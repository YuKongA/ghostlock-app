package com.ghostlock.app.shizuku

import android.content.Context
import android.os.Process
import androidx.annotation.Keep
import com.ghostlock.app.domain.model.SupportedKernels
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

@Keep
class GhostlockUserService(private val context: Context) : IGhostlockUserService.Stub() {
    private val running = AtomicBoolean(false)

    override fun runExploit(
        primaryCpu: Int,
        consumerCpu: Int,
        safeMode: Boolean,
        callback: IGhostlockCallback,
    ) {
        if (!running.compareAndSet(false, true)) {
            callback.onLog("error: another GhostLock process is already running")
            callback.onComplete(1)
            return
        }
        Thread({
            val exitCode = runCatching {
                require(Process.myUid() == Process.SHELL_UID) {
                    "Shizuku UserService uid=${Process.myUid()}, expected ${Process.SHELL_UID}"
                }
                val status = File("/proc/self/status").readText()
                require(Regex("(?m)^Seccomp:\\s*0$").containsMatchIn(status)) {
                    "Shizuku UserService is still seccomp-filtered"
                }
                val release = System.getProperty("os.version", "").orEmpty()
                require(release in SupportedKernels.REQUIRES_SHIZUKU) {
                    "kernel does not require Shizuku: $release"
                }

                val binary = File(context.applicationInfo.nativeLibraryDir, "libghostlock.so")
                require(binary.isFile) { "missing GhostLock binary: ${binary.absolutePath}" }

                val workDir = File("/data/local/tmp/ghostlock-app").apply {
                    require(isDirectory || mkdirs()) { "cannot create $absolutePath" }
                }
                callback.onLog("Shizuku ready: uid=${Process.myUid()} Seccomp=0")
                callback.onLog("kernel: $release")
                val nativeLog = File(workDir, ".ghostlock_native.log")
                ProcessBuilder(binary.absolutePath)
                    .directory(workDir)
                    .redirectErrorStream(true)
                    .apply {
                        environment()["GHOSTLOCK_HOME"] = workDir.absolutePath
                        environment()["TMPDIR"] = workDir.absolutePath
                        environment()["HOME"] = workDir.absolutePath
                        environment()["GHOSTLOCK_CORE"] = primaryCpu.toString()
                        environment()["GHOSTLOCK_CONSUMER_CORE"] = consumerCpu.toString()
                        if (safeMode) environment()["GHOSTLOCK_DISABLE_MODULES"] = "1"
                    }
                    .start()
                    .let { process ->
                        nativeLog.bufferedWriter().use { persistentLog ->
                            process.inputStream.bufferedReader().useLines { lines ->
                                lines.forEach { line ->
                                    persistentLog.appendLine(line)
                                    persistentLog.flush()
                                    callback.onLog(line)
                                }
                            }
                        }
                        process.waitFor()
                    }
            }.getOrElse { error ->
                runCatching { callback.onLog("error: ${error.message}") }
                1
            }
            running.set(false)
            runCatching { callback.onComplete(exitCode) }
        }, "ghostlock-shizuku-runner").start()
    }

    override fun destroy() {
        if (!running.get()) kotlin.system.exitProcess(0)
    }
}
