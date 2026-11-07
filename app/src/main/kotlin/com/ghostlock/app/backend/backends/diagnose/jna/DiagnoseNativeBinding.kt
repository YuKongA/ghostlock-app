package com.ghostlock.app.backend.backends.diagnose.jna

import android.os.ParcelFileDescriptor
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogEmitter
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.sun.jna.Native
import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/**
 * Concrete owner of the Diagnose JNA mapping and each synchronous native chain's two pipe pairs.
 *
 * The production constructor performs the sole constant-name load during RunHost prepare. Native and Shell
 * descriptors are borrowed only for one call; [close] terminates Java-owned pipe resources without unloading
 * the process-lifetime DSO.
 */
internal class DiagnoseNativeBinding private constructor(
    private val library: DiagnoseJnaLibrary?,
    private val chainOverride: ((LogEmitter, Long) -> ChainResult)?,
    private val closeOverride: (() -> Unit)?,
) : Closeable {
    private val closed = AtomicBoolean(false)
    private val activeCall = AtomicReference<ChainResources?>(null)

    /** Loads the sole project DSO by its immutable logical name. */
    constructor() : this(
        library = Native.load("ghostlock_diagnose", DiagnoseJnaLibrary::class.java),
        chainOverride = null,
        closeOverride = null,
    )

    /** Stores an already mapped library for direct call tests without introducing another loader path. */
    internal constructor(library: DiagnoseJnaLibrary) : this(library, null, null)

    /**
     * Supplies a host-only synchronous seam for Flow state tests whose JVM cannot load the Android DSO.
     *
     * @param chain Exact replacement result; it owns any test log injection.
     * @param closeAction Observable binding close action.
     */
    internal constructor(
        chain: (LogEmitter, Long) -> ChainResult,
        closeAction: () -> Unit = {},
    ) : this(null, chain, closeAction)

    /** Immutable Kotlin projection of one completed release-coupled ordinary-C invocation. */
    data class ChainResult(
        val status: Int,
        val uid: Int,
        val pid: Int,
        val echoedNonce: Long,
        val handoffStatus: Int,
        val nativeLogValid: Boolean,
        val shellLogValid: Boolean,
    )

    /**
     * Runs the single native chain, drains both pipes to EOF, and injects independently validated records.
     *
     * @param logs Current RunHost log producer boundary; delivery failure never changes returned facts.
     * @param nonce Signed Kotlin representation of the raw unsigned 64-bit nonce bits.
     * @return Function status, identity, handoff status, and independent phase-log validity.
     * @throws IllegalStateException When closed or another invocation still owns the binding.
     * @throws java.io.IOException When pipe creation, JNA plumbing, or drain fails.
     */
    fun runChain(logs: LogEmitter, nonce: Long): ChainResult {
        check(!closed.get()) { "diagnose native binding is closed" }
        chainOverride?.let { chain -> return chain(logs, nonce) }
        val request = DiagnoseJnaLibrary.Request.ByReference()
        val result = DiagnoseJnaLibrary.Result.ByReference()

        val resources = ChainResources.create()
        check(activeCall.compareAndSet(null, resources)) { "diagnose native call is already active" }
        try {
            request.nonce = nonce
            request.nativeLogFd = resources.native.writeFd()
            request.shellLogFd = resources.shell.writeFd()
            result.uid = 0
            result.pid = 0
            result.echoedNonce = 0L
            result.handoffStatus = HANDOFF_NOT_RUN
            request.write()
            result.write()

            val status = checkNotNull(library) { "diagnose mapped library is missing" }
                .gl_diagnose_run(request, result)
            result.read()
            resources.closeWriteEnds()
            val nativeBytes = resources.native.drainReadEnd()
            val shellBytes = resources.shell.drainReadEnd()
            val nativeLogValid = decodeRecord(nativeBytes, NATIVE_RECORD)
            val shellLogValid = decodeRecord(shellBytes, SHELL_RECORD)
            if (nativeLogValid) emit(logs, LogOrigin.Native, NATIVE_BODY)
            if (shellLogValid) emit(logs, LogOrigin.Shell, SHELL_BODY)
            return ChainResult(
                status = status,
                uid = result.uid,
                pid = result.pid,
                echoedNonce = result.echoedNonce,
                handoffStatus = result.handoffStatus,
                nativeLogValid = nativeLogValid,
                shellLogValid = shellLogValid,
            )
        } finally {
            resources.close()
            activeCall.compareAndSet(resources, null)
        }
    }

    /** Marks this owner closed and terminates only pipe endpoints still owned by an active call. */
    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        activeCall.getAndSet(null)?.close()
        closeOverride?.invoke()
    }

    /** Decodes exactly one bounded phase record while discarding its source elapsed value. */
    private fun decodeRecord(bytes: ByteArray?, pattern: Regex): Boolean {
        if (bytes == null || bytes.isEmpty() || bytes.size > MAX_STREAM_BYTES) return false
        if (bytes.last() != '\n'.code.toByte()) return false
        if (bytes.dropLast(1).any { byte ->
                byte == '\n'.code.toByte() || (byte.toInt() and 0xff) !in ASCII_PRINTABLE
            }
        ) {
            return false
        }
        val match = pattern.matchEntire(bytes.toString(Charsets.US_ASCII)) ?: return false
        return match.groupValues[1].toLongOrNull() != null
    }

    /** Delivers one validated phase record without changing chain facts on sink failure. */
    private fun emit(logs: LogEmitter, origin: LogOrigin, body: String) {
        runCatching { logs.emit(LogDraft(origin, LogLevel.Info, body)) }
    }

    /** Owns the two independent short pipes for one synchronous chain. */
    private class ChainResources private constructor(
        val native: CallResources,
        val shell: CallResources,
    ) : Closeable {
        /** Closes both caller-owned write endpoints after native has detached its duplicates. */
        fun closeWriteEnds() {
            native.closeWriteEnd()
            shell.closeWriteEnd()
        }

        /** Closes every pipe endpoint still owned by this invocation. */
        override fun close() {
            native.close()
            shell.close()
        }

        companion object {
            /** @return Two newly created independent pipe pairs. */
            fun create(): ChainResources = ChainResources(CallResources.create(), CallResources.create())
        }
    }

    /** Thread-safe ownership wrapper for one Java pipe pair. */
    private class CallResources private constructor(
        private var readEnd: ParcelFileDescriptor?,
        private var writeEnd: ParcelFileDescriptor?,
    ) : Closeable {
        /** @return Borrowed raw write descriptor valid only until [closeWriteEnd]. */
        @Synchronized
        fun writeFd(): Int = checkNotNull(writeEnd) { "diagnose native write end is closed" }.fd

        /** Closes the caller-owned write endpoint so native duplicate closure produces EOF. */
        @Synchronized
        fun closeWriteEnd() {
            writeEnd?.close()
            writeEnd = null
        }

        /** Drains through EOF while retaining at most 769 bytes. */
        fun drainReadEnd(): ByteArray? {
            val descriptor = synchronized(this) {
                checkNotNull(readEnd) { "diagnose native read end is closed" }.also { readEnd = null }
            }
            return ParcelFileDescriptor.AutoCloseInputStream(descriptor).use { input ->
                val captured = ByteArrayOutputStream(MAX_CAPTURE_BYTES)
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                var total = 0L
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (captured.size() < MAX_CAPTURE_BYTES) {
                        val retained = minOf(count, MAX_CAPTURE_BYTES - captured.size())
                        captured.write(buffer, 0, retained)
                    }
                    total += count.toLong()
                }
                if (total <= MAX_STREAM_BYTES.toLong()) captured.toByteArray() else null
            }
        }

        /** Closes only endpoints still owned by this call wrapper. */
        @Synchronized
        override fun close() {
            runCatching { writeEnd?.close() }
            runCatching { readEnd?.close() }
            writeEnd = null
            readEnd = null
        }

        companion object {
            /** @return One newly created blocking pipe whose two endpoints are owned by the caller. */
            fun create(): CallResources {
                val endpoints = ParcelFileDescriptor.createPipe()
                check(endpoints.size == 2) { "diagnose native pipe endpoint count mismatch" }
                return CallResources(endpoints[0], endpoints[1])
            }
        }
    }

    companion object {
        const val STATUS_OK: Int = 0
        const val HANDOFF_NOT_RUN: Int = Int.MIN_VALUE
        const val HANDOFF_HOME_MISSING: Int = -1
        const val HANDOFF_HOME_INVALID: Int = -2
        const val HANDOFF_OPEN_HOME: Int = -3
        const val HANDOFF_OPEN_DIRECTORY: Int = -4
        const val HANDOFF_OPEN_SCRIPT: Int = -5
        const val HANDOFF_SCRIPT_TYPE: Int = -6
        const val HANDOFF_SCRIPT_MAGIC: Int = -7
        const val HANDOFF_SPAWN_CALL: Int = -8
        const val HANDOFF_WAIT: Int = -9
        const val HANDOFF_CHILD_SIGNALED: Int = -10
        const val HANDOFF_INTERNAL: Int = -11
        const val MAX_STREAM_BYTES: Int = 768
        const val MAX_CAPTURE_BYTES: Int = MAX_STREAM_BYTES + 1
        const val NATIVE_BODY: String = "diagnose:native:roundtrip-ok"
        const val SHELL_BODY: String = "diagnose:shell:handoff-ok"

        private val ASCII_PRINTABLE: IntRange = 0x20..0x7e
        private val NATIVE_RECORD = Regex("\\[I/([0-9]+)ms] diagnose:native:roundtrip-ok\\n")
        private val SHELL_RECORD = Regex("\\[I/([0-9]+)ms] diagnose:shell:handoff-ok\\n")
    }
}
