package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.core.access.RelayAccessService
import com.ghostlock.app.core.rootchannel.RootChannelClient
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * 收尾脚本执行器：执行 files/finish/pending.sh。
 *
 * 若收尾配置开启「relay 后开启内核内存访问接口」，先启动 [RelayAccessService]
 * 后台窗口，再由脚本自己完成 5 秒握手与等待；脚本结束时窗口随之关闭。
 */
object FinishExecution {
    private const val PREFS = "ghostlock.finish"
    private const val KEY = "pending_script"

    fun pendingScript(context: Context): File {
        val recorded = context.getSharedPreferences(PREFS, 0).getString(KEY, null)
        val fromPrefs = recorded?.let(::File)
        if (fromPrefs != null && fromPrefs.isFile) return fromPrefs
        return File(context.filesDir, "finish/pending.sh")
    }

    /** 执行收尾脚本；返回退出码（-1 = 脚本不存在或启动失败）。 */
    fun run(context: Context, emit: (LogLevel, String) -> Unit): Int {
        val script = FinishParams.save(context, FinishConfig.state)
        if (!script.isFile) {
            emit(LogLevel.Error, "finish: params missing: " + script.absolutePath)
            return -1
        }
        emit(LogLevel.Verbose, "finish: start params=" + script.absolutePath + " bytes=" + script.length())
        if (FinishConfig.state.kernelAccessAfterRelay) {
            Thread({
                runCatching { RelayAccessService.serve { line -> emit(LogLevel.Info, line) } }
                    .onFailure { emit(LogLevel.Error, "relay access: " + (it.message ?: "failed")) }
            }, "relay-access").apply { isDaemon = true }.start()
        }
        /* root_cmd 直接执行收尾：请求文件 + 结果文件，避免 SELinux socket 限制。 */
        val bridged = runViaRootBridge(context, emit)
        if (bridged != null) {
            emit(if (bridged == 0) LogLevel.Info else LogLevel.Error, "finish: exit=" + bridged)
            return bridged
        }

        /* 旧路径：rootd socket；没有 bridge 时再尝试。 */
        if (RootChannelClient.available(context)) {
            emit(LogLevel.Info, "finish: root channel path=" + RootChannelClient.socketPath(context))
            val code = RootChannelClient.exec(context, "sh " + quote(script.absolutePath)) { line ->
                emit(levelOf(line), line)
            }
            if (code != null) {
                emit(if (code == 0) LogLevel.Info else LogLevel.Error, "finish: exit=" + code)
                return code
            }
            emit(LogLevel.Warn, "finish: root channel failed; falling back to su")
        }

        val su = suBinary()
        emit(LogLevel.Info, "finish: su=" + su)
        val process = runCatching {
            ProcessBuilder(su, "-c", "sh " + script.absolutePath)
                .redirectErrorStream(true)
                .start()
        }.getOrElse { error ->
            emit(LogLevel.Error, "finish: su failed: " + (error.message ?: "unknown"))
            return -1
        }
        process.inputStream.bufferedReader().useLines { lines ->
            lines.forEach { line -> emit(levelOf(line), line) }
        }
        if (!process.waitFor(120, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            emit(LogLevel.Warn, "finish: timeout")
            return -1
        }
        emit(if (process.exitValue() == 0) LogLevel.Info else LogLevel.Error, "finish: exit=" + process.exitValue())
        return process.exitValue()
    }

    /** 攻击链留下的 root watcher：请求文件 + 结果文件，全在 /data/local/tmp。 */
    private fun runViaRootBridge(context: Context, emit: (LogLevel, String) -> Unit): Int? {
        val dir = java.io.File("/data/local/tmp")
        val ready = java.io.File(dir, ".ghostlock_finish.ready")
        if (!ready.isFile) return null
        if (System.currentTimeMillis() - ready.lastModified() > 10 * 60 * 1000L) {
            emit(LogLevel.Warn, "finish: root bridge stale; falling back")
            return null
        }
        val request = java.io.File(context.filesDir, "finish/root-request").apply { parentFile?.mkdirs() }
        runCatching { request.writeText("1") }.onFailure {
            emit(LogLevel.Warn, "finish: root bridge request failed: " + it.message)
            return null
        }
        val log = java.io.File(dir, ".ghostlock_finish.log")
        val exitFile = java.io.File(dir, ".ghostlock_finish.exit")
        emit(LogLevel.Info, "finish: root bridge")
        var offset = 0L
        var waited = 0L
        while (waited < 120_000L) {
            offset = drainBridgeLog(log, offset, emit)
            if (exitFile.isFile) {
                val code = runCatching { exitFile.readText().trim().toInt() }.getOrDefault(-1)
                offset = drainBridgeLog(log, offset, emit)
                return code
            }
            Thread.sleep(100L)
            waited += 100L
        }
        emit(LogLevel.Error, "finish: root bridge timeout")
        return -1
    }

    private fun drainBridgeLog(log: java.io.File, offset: Long, emit: (LogLevel, String) -> Unit): Long {
        if (!log.isFile) return offset
        return runCatching {
            java.io.RandomAccessFile(log, "r").use { raf ->
                if (raf.length() <= offset) return offset
                raf.seek(offset)
                val bytes = ByteArray((raf.length() - offset).toInt())
                raf.readFully(bytes)
                val text = String(bytes, Charsets.UTF_8)
                val end = text.lastIndexOf('\n')
                if (end >= 0) {
                    text.substring(0, end).lineSequence().forEach { line -> emit(levelOf(line), line) }
                    offset + text.substring(0, end + 1).toByteArray(Charsets.UTF_8).size
                } else {
                    offset
                }
            }
        }.getOrDefault(offset)
    }
    /** shell 行本身没有级别：按“失败/缺失或降级/成功”映射。 */
    /** 优先 KernelPatch/FolkPatch 的 su（/system/bin/kp），否则回退系统 su。 */
    private fun suBinary(): String {
        val kp = java.io.File("/system/bin/kp")
        return if (kp.isFile && kp.canExecute()) kp.absolutePath else "su"
    }

    private fun quote(path: String): String = "'" + path.replace("'", "'\''") + "'"

    private fun levelOf(line: String): LogLevel = when {
        line.contains("failed", ignoreCase = true) ||
            line.contains("refused", ignoreCase = true) ||
            line.contains("missing", ignoreCase = true) -> LogLevel.Error
        line.contains("timeout", ignoreCase = true) ||
            line.contains("not found", ignoreCase = true) ||
            line.contains("skipped", ignoreCase = true) ||
            line.contains("continue", ignoreCase = true) -> LogLevel.Warn
        else -> LogLevel.Info
    }
}
