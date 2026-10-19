package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.net.Uri
import com.ghostlock.app.engine.pagecache.LkmImagePatch
import java.io.File

/**
 * 一次 run 的收尾会话：`run.sh` + 日志 memfd + 只读输入 memfd。
 *
 * [patcher] 在攻击写页缓存前把脚本路径写进 `.ko` 镜像；[finish] 在攻击结束后做
 * 最后一次 drain、删除脚本、关闭输入 fd，日志文件保留。
 */
class FinishSession internal constructor(
    private val context: Context,
    val script: File,
    private val log: LogMemfd,
    private val inputs: List<InputMemfd>,
) {
    /** 把脚本路径写进模块镜像副本。 */
    val patcher: (ByteArray) -> ByteArray = { image ->
        LkmImagePatch.apply(image, script.absolutePath)
    }

    /** 本次 run 的日志文件；攻击结束后仍保留。 */
    val logFile: File get() = log.file

    /** drainer 的最近一次 I/O 失败原因；成功路径为 null。 */
    val logError: String? get() = log.lastError

    /** 停止 drainer、删除脚本、关闭输入 fd；日志保留。 */
    fun finish() {
        log.stop()
        FinishScriptStore.delete(context)
        inputs.forEach { it.close() }
    }
}

/**
 * 收尾准备/清理的唯一入口。
 *
 * 准备阶段在攻击之前完成：读配置、建日志 memfd、解析输入（`file://` 直取路径，
 * `content://` 进 memfd）、渲染整段脚本、写 `run.sh`。任何一步失败都抛
 * [PrepException]，调用方不得继续攻击。
 */
object FinishExecution {
    /** 准备失败原因；`reason` 直接进日志。 */
    class PrepException(val reason: String) : Exception(reason)

    /**
     * 准备本次 run 的收尾。
     *
     * @return null 表示用户没有启用任何收尾项；否则返回会话。
     * @throws PrepException memfd、输入或脚本写入失败。
     */
    fun prepare(context: Context): FinishSession? {
        FinishConfig.load(context)
        val state = FinishConfig.state
        if (!state.requestsFinish()) return null
        val log = LogMemfd.open(context, System.currentTimeMillis())
            ?: throw PrepException("memfd-failed")
        log.start()
        if (log.lastError != null) {
            log.stop()
            throw PrepException("log-open-failed")
        }
        val inputs = mutableListOf<InputMemfd>()

        /** 只接受 `file://` 的真实路径；其余 URI 读进 memfd 并保持打开。 */
        fun resolveInput(uri: String?): String? {
            if (uri.isNullOrBlank()) return null
            val parsed = Uri.parse(uri)
            if (parsed.scheme == "file") return parsed.path
            val memfd = InputMemfd.fromUri(context, uri) ?: throw PrepException("input-failed")
            inputs += memfd
            return memfd.path
        }

        return try {
            val stagedScript = resolveInput(state.scriptUri)
            val manualLkm = resolveInput(state.lkmUri)
            val autoLkm = if (state.rootManager == "FolkPatch" && manualLkm == null) {
                FolkPatchKo.open(context)?.also { inputs += it }?.path
            } else {
                null
            }
            val script = state.toRootScript(
                managerBinary = null,
                stagedScript = stagedScript,
                stagedLkm = autoLkm ?: manualLkm,
                logPath = log.path,
            )
            FinishSession(context, FinishScriptStore.write(context, script), log, inputs.toList())
        } catch (failure: PrepException) {
            inputs.forEach { it.close() }
            log.stop()
            throw failure
        } catch (failure: Throwable) {
            inputs.forEach { it.close() }
            log.stop()
            throw PrepException("script-write-failed")
        }
    }

    /**
     * 等待本次 UMH 结束：`ghostlock` 在 `/proc/modules` 出现后消失。
     *
     * @return true 观察到出现在到无；false 超时或从未出现；null 视图不可读。
     */
    fun awaitCompletion(
        timeoutMs: Long = 30_000L,
        pollMs: Long = 50L,
        present: () -> Boolean? = ::modulePresent,
    ): Boolean? {
        val deadline = System.currentTimeMillis() + timeoutMs
        var seen = false
        while (System.currentTimeMillis() < deadline) {
            val now = runCatching { present() }.getOrNull()
            if (now == null) return null
            if (now) seen = true
            if (seen && !now) return true
            try {
                Thread.sleep(pollMs)
            } catch (_: InterruptedException) {
                return false
            }
        }
        return false
    }

    /** 模块是否在内核视图里；`/proc/modules` 读不到时退回 `/sys/module/ghostlock`。 */
    private fun modulePresent(): Boolean? = runCatching {
        java.io.File("/proc/modules").readText().lineSequence().any { it.startsWith("ghostlock ") }
    }.getOrElse {
        runCatching { java.io.File("/sys/module/ghostlock").exists() }.getOrNull()
    }

    /** 启动时清理上一轮残留的脚本；日志保留。 */
    fun cleanStale(context: Context) {
        FinishScriptStore.delete(context)
    }

    private fun FinishState.requestsFinish(): Boolean =
        rootManagerEnabled || runAppEnabled || scriptEnabled || lkmEnabled ||
            restoreSelinux || softRebootAfterFinish
}
