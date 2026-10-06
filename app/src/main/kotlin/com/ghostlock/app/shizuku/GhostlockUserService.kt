package com.ghostlock.app.shizuku

import android.content.Context
import android.os.Process
import androidx.annotation.Keep
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.profile.ChannelBStdin
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.profile.SessionSecretFrame
import java.io.File
import java.io.OutputStream
import java.io.RandomAccessFile
import java.util.concurrent.atomic.AtomicBoolean

@Keep
class GhostlockUserService(private val context: Context) : IGhostlockUserService.Stub() {
    private val running = AtomicBoolean(false)

    private companion object {
        const val StatusMarker = "\u001eGLK_STATUS"
        const val StatusAck = "\u001eGLK_STATUS_ACK\n"
        const val StatusDisabled = "\u001eGLK_STATUS_DISABLED"
    }

    override fun runExploit(
        primaryCpu: Int,
        consumerCpu: Int,
        safeMode: Boolean,
        forceAttack: Boolean,
        profileBlob: ByteArray,
        sessionFrame: ByteArray,
        debugDir: String?,
        callback: IGhostlockCallback,
        statusCallback: IGhostlockStatusCallback,
    ) {
        startAppCall(
            safeMode = safeMode,
            forceAttack = forceAttack,
            profileBlob = profileBlob,
            sessionFrame = sessionFrame,
            debugDir = debugDir,
            allowDevTarget = false,
            callback = callback,
            statusCallback = statusCallback,
        )
    }

    /**
     * One app-call run: validate the shell domain, write the length-prefixed
     * GLKv3 document (plus the optional channel-B frame) to native stdin, then
     * relay the native log file and answer the status ACKs. Production and the
     * dev entry share this body; only the argv flags differ.
     */
    private fun startAppCall(
        safeMode: Boolean,
        forceAttack: Boolean,
        profileBlob: ByteArray,
        sessionFrame: ByteArray,
        debugDir: String?,
        allowDevTarget: Boolean,
        callback: IGhostlockCallback,
        statusCallback: IGhostlockStatusCallback,
    ) {
        if (!running.compareAndSet(false, true)) {
            callback.onLog("<s> error: another GhostLock process is already running")
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
                require(profileBlob.size >= 16) { "profile blob is too short" }
                val binary = File(context.applicationInfo.nativeLibraryDir, "libghostlock.so")
                require(binary.isFile) { "missing GhostLock binary: ${binary.absolutePath}" }

                val workDir = File("/data/local/tmp/ghostlock-app").apply {
                    require(isDirectory || mkdirs()) { "cannot create $absolutePath" }
                }
                callback.onLog("<s> Shizuku ready: uid=${Process.myUid()} Seccomp=0")
                callback.onLog("<s> kernel: $release")
                val nativeLog = File(workDir, ".ghostlock_native.log")
                // U01-S14: per-run KernelSU log so a previous run's markers can
                // never satisfy the handoff probe; the native process receives
                // the resolved path via GHOSTLOCK_KSU_LOG.
                val ksuLog = File(workDir, "ghostlock-ksu-${System.currentTimeMillis()}.log")
                // GLKv3: safe_mode lives in the common section; there is no fixed
                // slot offset, so the codec locates and rewrites the map value.
                val effectiveBlob = if (safeMode) {
                    NativeProfileDocument.patchSafeMode(profileBlob) ?: profileBlob
                } else {
                    profileBlob
                }
                // Channel B: only cve_2026_43284 consumes the session frame.
                // Anything else sends the GLKv3 document alone, exactly as before.
                /* The wire carries canonical tokens: resolve EXACTLY, as native does. */
                val backend = Glkv3Decoder.decode(effectiveBlob)?.backend?.let(BackendKind::resolve)
                if (ChannelBStdin.requiresSessionFrame(backend)) {
                    require(sessionFrame.size ==
                        SessionSecretFrame.LENGTH_PREFIX_SIZE + SessionSecretFrame.PAYLOAD_SIZE) {
                        "cve_2026_43284 requires an 84-byte channel-B session frame"
                    }
                }
                val argv = appCallArgs(
                    binaryPath = binary.absolutePath,
                    forceAttack = forceAttack,
                    debugDir = debugDir,
                    allowDevTarget = allowDevTarget,
                )
                callback.onLog("<b> starting native: ${binary.absolutePath}")
                ProcessBuilder(argv)
                    .directory(workDir)
                    .redirectErrorStream(true)
                    .redirectOutput(nativeLog)
                    .apply {
                        environment()["GHOSTLOCK_HOME"] = workDir.absolutePath
                        environment()["TMPDIR"] = workDir.absolutePath
                        environment()["HOME"] = workDir.absolutePath
                        environment()["GHOSTLOCK_KSU_LOG"] = ksuLog.absolutePath
                    }
                    .start()
                    .let { process ->
                        val stdinOut = process.outputStream
                        /* Length-prefixed GLKv3, then -- for 43284 only -- the
                         * framed session secrets; stdin stays open for the ACK. */
                        runCatching {
                            val length = effectiveBlob.size
                            stdinOut.write(
                                byteArrayOf(
                                    (length ushr 24).toByte(),
                                    (length ushr 16).toByte(),
                                    (length ushr 8).toByte(),
                                    length.toByte(),
                                ),
                            )
                            stdinOut.write(effectiveBlob)
                            if (sessionFrame.isNotEmpty()) stdinOut.write(sessionFrame)
                            stdinOut.flush()
                        }
                        // The native process writes its log to a file and this
                        // tailer forwards lines asynchronously. Reading a pipe
                        // here applied backpressure inside the PI race window
                        // (every line also costs a binder round trip), which
                        // stalled the route and ended in a kernel panic.
                        val tailer = Thread({
                            relayLog(nativeLog, callback, statusCallback, stdinOut)
                        }, "ghostlock-shizuku-tailer").apply {
                            isDaemon = true
                            start()
                        }
                        val exitCode = process.waitFor()
                        callback.onLog("<b> native exited code=$exitCode")
                        Thread.sleep(200)
                        tailer.interrupt()
                        tailer.join(1000)
                        exitCode
                    }
            }.getOrElse { error ->
                runCatching { callback.onLog("<s> error: ${error.message}") }
                1
            }
            running.set(false)
            runCatching { callback.onComplete(exitCode) }
        }, "ghostlock-shizuku-runner").start()
    }

    /** Forward complete native log lines without ever blocking the native
     * process; the file is the transport, binder is only the display path. */
    /** Forwards status events to the app (persist) and ACKs the native process;
     *  returns true when the line was a status event. Every app-call entry
     *  answers the ACKs, so a dev run cannot stall the chain. */
    private fun handleStatusLine(
        line: String,
        statusCallback: IGhostlockStatusCallback?,
        stdinOut: OutputStream?,
    ): Boolean {
        if (statusCallback == null) return false
        if (!line.startsWith(StatusMarker)) return false
        if (line.contains(StatusDisabled)) {
            runCatching { statusCallback.onStatus("", "disabled") }
            return true
        }
        val parts = line.removePrefix(StatusMarker).trim().split(' ')
        if (parts.size >= 2) {
            runCatching { statusCallback.onStatus(parts[0], parts[1]) }
            if (stdinOut != null) {
                runCatching {
                    stdinOut.write(StatusAck.toByteArray(Charsets.UTF_8))
                    stdinOut.flush()
                }
            }
        }
        return true
    }

    private fun relayLog(
        logFile: File,
        callback: IGhostlockCallback,
        statusCallback: IGhostlockStatusCallback?,
        stdinOut: OutputStream?,
    ) {
        var offset = 0L
        val pending = StringBuilder()
        while (!Thread.currentThread().isInterrupted) {
            try {
                if (logFile.isFile) {
                    RandomAccessFile(logFile, "r").use { handle ->
                        if (offset > handle.length()) {
                            offset = 0
                            pending.clear()
                        }
                        handle.seek(offset)
                        while (true) {
                            val byte = handle.read()
                            if (byte == -1) break
                            if (byte == '\n'.code) {
                                val line = pending.toString()
                                pending.clear()
                                offset = handle.filePointer
                                if (line.isNotEmpty()) {
                                    if (!handleStatusLine(line, statusCallback, stdinOut)) {
                                        runCatching { callback.onLog(line) }
                                    }
                                }
                            } else {
                                pending.append(byte.toChar())
                            }
                        }
                    }
                }
                Thread.sleep(100)
            } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
                return
            } catch (_: Exception) {
                try {
                    Thread.sleep(200)
                } catch (_: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return
                }
            }
        }
    }

    /**
     * Shizuku-manager contract member (see the AIDL note): the manager calls it
     * when it stops or unbinds the service, so having no in-repo caller is
     * expected. Idle service process -> exit; a running exploit is left alone.
     */
    override fun destroy() {
        if (!running.get()) kotlin.system.exitProcess(0)
    }
}
