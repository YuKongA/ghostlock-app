package com.ghostlock.app.backend.cve43499.finish

import com.ghostlock.app.backend.cve43499.victim.VictimIdentity
import java.util.concurrent.TimeUnit

/**
 * 收尾脚本的实际执行证据（由 UI 侧 `FinishExecution` 产生；后端只校验）。
 *
 * @property runId 本次运行 id；必须与 handoff/验收输入一致（marker 内容绑定）。
 * @property exitCode 进程退出码；null 表示未拿到（启动失败/超时）。
 * @property timedOut 是否超时；超时会被强制 kill，不得视为成功。
 * @property outputDigestSha256 有界输出摘要（SHA-256，不留原文）；可为 null。
 * @note 本证据只记录「实际执行发生过什么」，不表达业务成功。
 * @note 阶段：F-D finish 执行链。
 */
internal data class FinishExecutionEvidence(
    val runId: String,
    val exitCode: Int?,
    val timedOut: Boolean,
    val outputDigestSha256: String?,
    /** 退出后读回的成功 marker 内容（应为 runId；null=未读到）。 */
    val successMarkerContent: String?,
    /** 退出后读回的失败 marker 内容（非空=脚本自报失败）。 */
    val failureMarkerContent: String?,
)

/**
 * 一次有界命令执行的结果（只保存摘要，不保存 terminal 原文）。
 *
 * @property exitCode 进程退出码；null 表示未拿到。
 * @property timedOut 是否超时（超时即 kill，不得作为正证据）。
 * @property outputDigestSha256 有界输出摘要；可为 null。
 */
internal data class FinishCommandResult(
    val exitCode: Int?,
    val timedOut: Boolean,
    val outputDigestSha256: String?,
)

/**
 * 可注入的有界命令 seam（ksud/root 管理器证据用）。
 *
 * @note 验收只使用「退出码 + 输出摘要」，不使用 terminal 文本作为成功证据。
 * @note 实现必须有界：超时必须 kill 并在 [FinishCommandResult.timedOut] 标记。
 */
internal interface FinishCommandSource {
    /**
     * 执行一条固定命令。
     *
     * @param command 命令与参数（由验收侧常量给出，不拼接用户输入）。
     * @param timeoutMs 有界超时（毫秒）。
     * @return 有界执行结果。
     */
    fun run(command: List<String>, timeoutMs: Long): FinishCommandResult
}

/**
 * 生产命令 seam：`ProcessBuilder` + 有界等待 + 输出摘要。
 *
 * @note 不使用 terminal 文本判定结果；输出只读取最多 [MAX_OUTPUT_BYTES] 字节并计算 SHA-256。
 * @note 阶段：F-D ksud 证据。
 */
internal object ProcessBuilderFinishCommandSource : FinishCommandSource {
    /** 输出摘要最多读取的字节数。 */
    private const val MAX_OUTPUT_BYTES: Int = 4096

    override fun run(command: List<String>, timeoutMs: Long): FinishCommandResult {
        val process = runCatching {
            ProcessBuilder(command).redirectErrorStream(true).start()
        }.getOrNull() ?: return FinishCommandResult(null, false, null)
        val finished = runCatching {
            process.waitFor(timeoutMs, TimeUnit.MILLISECONDS)
        }.getOrDefault(false)
        if (!finished) {
            runCatching { process.destroyForcibly() }
            return FinishCommandResult(null, true, null)
        }
        val output = runCatching { readBounded(process) }.getOrNull()
        val exitCode = runCatching { process.exitValue() }.getOrNull()
        val digest = output?.takeIf { it.isNotEmpty() }?.let {
            FinishHandoff.sha256Hex(it)
        }
        return FinishCommandResult(exitCode, false, digest)
    }

    /**
     * 有界读取子进程输出。
     *
     * @param process 已结束的进程。
     * @return 最多 [MAX_OUTPUT_BYTES] 字节的输出；读失败/空输出返回空数组。
     */
    private fun readBounded(process: Process): ByteArray {
        val stream = process.inputStream
        val buffer = ByteArray(MAX_OUTPUT_BYTES)
        var total = 0
        while (total < buffer.size) {
            val read = stream.read(buffer, total, buffer.size - total)
            if (read <= 0) break
            total += read
        }
        return if (total <= 0) ByteArray(0) else buffer.copyOf(total)
    }
}

/**
 * victim ↔ Actor 的可审计绑定证据：run 在启动 victim 时采集、execute 之后复查。
 *
 * @property runId 本次运行 id（与 handoff/marker/执行证据绑定）。
 * @property victim execute 前采集的 victim 身份（PID/starttime/uid）。
 * @property primitiveWaiterAddress prepareActor 实际绑定的最终 waiter 地址。
 * @property primitiveTaskAddress prepareActor 实际绑定的最终 task 地址。
 * @property primitiveLockAddress prepareActor 实际绑定的最终 lock 地址。
 * @property victimStartTimeAfterExecute execute 之后复查到的 starttime；必须与
 *   [VictimIdentity.startTimeTicks] 相同，证明目标进程未被替换。
 * @note 绑定缺失/不一致即拒绝；不得用日志或 terminal 输出替代。
 * @note 阶段：F-D victim↔Actor 绑定。
 */
internal data class RunBindingEvidence(
    val runId: String,
    val victim: VictimIdentity,
    val primitiveWaiterAddress: Long,
    val primitiveTaskAddress: Long,
    val primitiveLockAddress: Long,
    val victimStartTimeAfterExecute: Long,
)
