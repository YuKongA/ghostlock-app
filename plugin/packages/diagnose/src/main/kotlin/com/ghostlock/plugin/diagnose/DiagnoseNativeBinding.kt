package com.ghostlock.plugin.diagnose

import android.os.ParcelFileDescriptor
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.PluginLogOrigin
import com.sun.jna.Library
import com.sun.jna.Native
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

/** Plugin-owned JNA loader, native call owner and short-lived pipe owner. */
internal class DiagnoseNativeBinding private constructor(
    private val library: DiagnoseJnaLibrary?,
    private val chainOverride: ((DiagnosticRunContext, Long) -> ChainResult)?,
    private val closeOverride: (() -> Unit)?,
) : AutoCloseable {
    private val closed = AtomicBoolean(false)
    private val activeCall = AtomicReference<ChainResources?>(null)

    constructor() : this(
        library = loadLibrary(),
        chainOverride = null,
        closeOverride = null,
    )

    internal constructor(
        chain: (DiagnosticRunContext, Long) -> ChainResult,
        closeAction: () -> Unit = {},
    ) : this(null, chain, closeAction)

    data class ChainResult(
        val status: Int,
        val uid: Int,
        val pid: Int,
        val echoedNonce: Long,
        val handoffStatus: Int,
        val nativeLogValid: Boolean,
        val shellLogValid: Boolean,
    )

    fun runChain(context: DiagnosticRunContext, nonce: Long): ChainResult {
        check(!closed.get()) { "Diagnose native binding is closed" }
        chainOverride?.let { return it(context, nonce) }
        val resources = ChainResources.create()
        check(activeCall.compareAndSet(null, resources)) { "Diagnose native call is already active" }
        try {
            val request = DiagnoseJnaLibrary.Request.ByReference().also {
                it.nonce = nonce
                it.nativeLogFd = resources.native.writeFd()
                it.shellLogFd = resources.shell.writeFd()
                it.write()
            }
            val result = DiagnoseJnaLibrary.Result.ByReference().also {
                it.uid = 0
                it.pid = 0
                it.echoedNonce = 0L
                it.handoffStatus = HANDOFF_NOT_RUN
                it.write()
            }
            val status = checkNotNull(library) { "Diagnose mapped library is missing" }
                .gl_diagnose_run(request, result)
            result.read()
            resources.closeWriteEnds()
            return ChainResult(
                status = status,
                uid = result.uid,
                pid = result.pid,
                echoedNonce = result.echoedNonce,
                handoffStatus = result.handoffStatus,
                nativeLogValid = resources.native.collect(
                    context,
                    PluginLogOrigin.NATIVE,
                    NATIVE_BODY,
                ),
                shellLogValid = resources.shell.collect(
                    context,
                    PluginLogOrigin.SHELL,
                    SHELL_BODY,
                ),
            )
        } finally {
            resources.close()
            activeCall.compareAndSet(resources, null)
        }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        activeCall.getAndSet(null)?.close()
        closeOverride?.invoke()
    }

    private class ChainResources private constructor(
        val native: PipeResources,
        val shell: PipeResources,
    ) : AutoCloseable {
        fun closeWriteEnds() {
            native.closeWriteEnd()
            shell.closeWriteEnd()
        }

        override fun close() {
            native.close()
            shell.close()
        }

        companion object {
            fun create(): ChainResources = ChainResources(PipeResources.create(), PipeResources.create())
        }
    }

    private class PipeResources private constructor(
        private var readEnd: ParcelFileDescriptor?,
        private var writeEnd: ParcelFileDescriptor?,
    ) : AutoCloseable {
        @Synchronized
        fun writeFd(): Int = checkNotNull(writeEnd) { "Diagnose pipe write end is closed" }.fd

        @Synchronized
        fun closeWriteEnd() {
            writeEnd?.close()
            writeEnd = null
        }

        @Synchronized
        fun collect(
            context: DiagnosticRunContext,
            origin: PluginLogOrigin,
            body: String,
        ): Boolean {
            val descriptor = readEnd ?: return false
            readEnd = null
            return PluginPipeLogCollector.collect(descriptor, context, origin, body)
        }

        @Synchronized
        override fun close() {
            runCatching { writeEnd?.close() }
            runCatching { readEnd?.close() }
            writeEnd = null
            readEnd = null
        }

        companion object {
            fun create(): PipeResources {
                val descriptors = ParcelFileDescriptor.createPipe()
                check(descriptors.size == 2) { "Diagnose pipe endpoint count mismatch" }
                return PipeResources(descriptors[0], descriptors[1])
            }
        }
    }

    companion object {
        /** Disables every JNA extraction/classpath fallback before the first active Native reference. */
        private fun loadLibrary(): DiagnoseJnaLibrary {
            System.setProperty("jna.nounpack", "true")
            System.setProperty("jna.noclasspath", "true")
            return Native.load(
                "ghostlock_diagnose",
                DiagnoseJnaLibrary::class.java,
                mapOf(Library.OPTION_CLASSLOADER to DiagnoseNativeBinding::class.java.classLoader),
            )
        }

        const val STATUS_OK = 0
        const val HANDOFF_NOT_RUN = Int.MIN_VALUE
        const val HANDOFF_HOME_MISSING = -1
        const val HANDOFF_HOME_INVALID = -2
        const val HANDOFF_OPEN_HOME = -3
        const val HANDOFF_OPEN_DIRECTORY = -4
        const val HANDOFF_OPEN_SCRIPT = -5
        const val HANDOFF_SCRIPT_TYPE = -6
        const val HANDOFF_SCRIPT_MAGIC = -7
        const val HANDOFF_SPAWN_CALL = -8
        const val HANDOFF_WAIT = -9
        const val HANDOFF_CHILD_SIGNALED = -10
        const val HANDOFF_INTERNAL = -11
        const val NATIVE_BODY = "diagnose:native:roundtrip-ok"
        const val SHELL_BODY = "diagnose:shell:handoff-ok"
    }
}
