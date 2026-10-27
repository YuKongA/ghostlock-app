package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.backend.cve43499.finish.FinishAcceptance
import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * 收尾脚本的**实际执行链**（F-D）。
 *
 * 职责边界：
 * - 只执行 UI 侧已经冻结好的脚本对象（[FinishHandoff.scriptPath]）；不生成、不选择、
 *   不解释脚本，也不改既有 Preview/UI 行为；
 * - root 上下文（uid==0）直接 `/system/bin/sh <script>`；否则经 `su -c` seam 提权；
 * - 有界等待：超时即 `destroyForcibly` 并标记 `timedOut`（不得视为成功）；
 * - 退出后用 `GHOSTLOCK_RUN_ID` 注入的 runId 读回 marker 内容（成功/失败两个 marker）；
 * - 产出 [FinishExecutionEvidence] 交给后端验收（后端不 import ui）。
 *
 * @param commandFactory 命令构造 seam：入参为 (命令, 环境变量)；宿主验证可注入 fake。
 * @param uidProvider 当前 uid 探测 seam；默认 `android.os.Process.myUid()`。
 * @param timeoutMs 有界执行超时（毫秒）。
 * @param maxOutputBytes 输出摘要最多读取的字节数。
 * @note 阶段：F-D finish 执行链；真机门禁前不接默认 Flow/UI 入口。
 */
internal class FinishExecution(
    private val commandFactory: (List<String>, Map<String, String>) -> Process = { command, environment ->
        val builder = ProcessBuilder(command).redirectErrorStream(true)
        builder.environment().putAll(environment)
        builder.start()
    },
    private val uidProvider: () -> Int = { android.os.Process.myUid() },
    private val timeoutMs: Long = DEFAULT_TIMEOUT_MS,
    private val maxOutputBytes: Int = MAX_OUTPUT_BYTES,
) {
    /**
     * 执行一次收尾脚本并返回执行证据。
     *
     * @param handoff UI 生成的不可变脚本对象。
     * @param runId 本次运行 id；经环境变量 `GHOSTLOCK_RUN_ID` 注入脚本。
     * @param logPath 脚本日志路径（环境变量 `GHOSTLOCK_LOG`）；null 表示由脚本/执行层决定。
     * @return 执行证据；参数不合法/启动失败/超时都以具名 null 字段表达，不抛异常。
     * @note 不把 terminal 文本当成功证据；输出只保留 SHA-256 摘要。
     */
    fun execute(
        handoff: FinishHandoff,
        runId: String,
        logPath: String? = null,
    ): FinishExecutionEvidence {
        if (handoff.scriptPath.isBlank() || runId.isBlank()) {
            return evidence(runId, null, false, null)
        }
        val asRoot = runCatching { uidProvider() }.getOrDefault(-1) == 0
        val command = if (asRoot) {
            listOf("/system/bin/sh", handoff.scriptPath)
        } else {
            listOf("su", "-c", "/system/bin/sh " + shQuote(handoff.scriptPath))
        }
        val environment = buildMap {
            put(ENV_RUN_ID, runId)
            if (logPath != null) put(ENV_LOG_PATH, logPath)
        }
        val process = runCatching { commandFactory(command, environment) }.getOrNull()
            ?: return evidence(runId, null, false, null)
        val finished = runCatching {
            process.waitFor(timeoutMs, TimeUnit.MILLISECONDS)
        }.getOrDefault(false)
        if (!finished) {
            runCatching { process.destroyForcibly() }
            return evidence(runId, null, true, null)
        }
        val output = runCatching { readBounded(process) }.getOrNull()
        val exitCode = runCatching { process.exitValue() }.getOrNull()
        val digest = output?.takeIf { it.isNotEmpty() }?.let {
            FinishHandoff.sha256Hex(it)
        }
        val successMarker = readMarker(FinishAcceptance.SUCCESS_MARKER_PATH)
        val failureMarker = readMarker(FinishAcceptance.FAILURE_MARKER_PATH)
        return FinishExecutionEvidence(
            runId = runId,
            exitCode = exitCode,
            timedOut = false,
            outputDigestSha256 = digest,
            successMarkerContent = successMarker,
            failureMarkerContent = failureMarker,
        )
    }

    /** @brief 读回一个 marker 的内容；不存在/不可读返回 null。 */
    private fun readMarker(path: String): String? = runCatching {
        val file = File(path)
        if (!file.exists()) null else file.readText().trim()
    }.getOrNull()

    /** @brief 有界读取子进程输出（最多 maxOutputBytes）。 */
    private fun readBounded(process: Process): ByteArray {
        val stream = process.inputStream
        val buffer = ByteArray(maxOutputBytes)
        var total = 0
        while (total < buffer.size) {
            val read = stream.read(buffer, total, buffer.size - total)
            if (read <= 0) break
            total += read
        }
        return if (total <= 0) ByteArray(0) else buffer.copyOf(total)
    }

    /** @brief 构造启动失败/超时的空证据。 */
    private fun evidence(
        runId: String,
        exitCode: Int?,
        timedOut: Boolean,
        digest: String?,
    ): FinishExecutionEvidence = FinishExecutionEvidence(
        runId = runId,
        exitCode = exitCode,
        timedOut = timedOut,
        outputDigestSha256 = digest,
        successMarkerContent = null,
        failureMarkerContent = null,
    )

    companion object {
        /** 默认有界执行超时（毫秒）。 */
        internal const val DEFAULT_TIMEOUT_MS: Long = 30_000L

        /** 输出摘要最多读取的字节数。 */
        internal const val MAX_OUTPUT_BYTES: Int = 4096

        /** 运行 id 环境变量名（脚本头 `RUN_ID` 读取）。 */
        internal const val ENV_RUN_ID: String = "GHOSTLOCK_RUN_ID"

        /** 日志路径环境变量名（执行层使用，脚本可选读取）。 */
        internal const val ENV_LOG_PATH: String = "GHOSTLOCK_LOG"
    }
}
