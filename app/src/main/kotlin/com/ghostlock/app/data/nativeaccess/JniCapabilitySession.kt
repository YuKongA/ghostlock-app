package com.ghostlock.app.data.nativeaccess

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.AccessSession as LegacyAccessSession
import com.ghostlock.app.core.CleanupDisposition
import com.ghostlock.app.core.SessionPhase
import com.ghostlock.app.core.access.AccessSession
import com.ghostlock.app.core.access.CapabilityDescriptor
import com.ghostlock.app.core.access.InitializationReceipt
import com.ghostlock.app.core.access.PageCacheDescriptor
import com.ghostlock.app.core.access.PageCacheKernelAccess
import com.ghostlock.app.core.access.PageCacheTarget
import com.ghostlock.app.core.access.SessionSnapshot
import com.ghostlock.app.core.access.ShutdownReceipt

/**
 * 能力会话：在既有 JNI 会话之上发布能力描述符。
 *
 * 描述符只来自 native Snapshot；没有发布 page-cache 标志时能力为 null，不按 CVE 猜。
 */
class JniCapabilitySession(
    private val native: JniAccessSession,
    target: PageCacheTarget,
    private val initParams: PageCacheInit43284? = null,
) : AccessSession, LegacyAccessSession {
    private val page = PageCacheKernelAccess(JniPageCacheOps(native, target), blockWidth = 16)
    private var initialized = false

    override val phase: SessionPhase get() = native.phase
    override val cleanup: CleanupDisposition get() = native.cleanup

    override fun close() {
        initialized = false
        native.close()
    }

    override val capabilities = object : com.ghostlock.app.core.access.AccessCapabilities {
        override val kernelMemory: com.ghostlock.app.core.access.KernelMemoryAccess? = null
        override val pageCache: com.ghostlock.app.core.access.PageCacheAccess?
            get() = if (initialized) page.pageCache else null
    }

    override suspend fun initialize(): AccessResult<InitializationReceipt> {
        val values = if (initParams != null) {
            mapOf(NativeProtocol.Key.Sequence to initParams.sequence.toULong())
        } else {
            emptyMap()
        }
        val blobs = if (initParams != null) {
            mapOf(
                NativeProtocol.Key.Secret to initParams.secret(),
                NativeProtocol.Key.Path to initParams.carrierPath.toByteArray(),
            )
        } else {
            emptyMap()
        }
        val init = native.invoke(NativeProtocol.Op.Initialize, values, blobs)
        if (init is AccessResult.Failure) return init
        val ready = page.initialize()
        initialized = ready is AccessResult.Success
        return ready
    }

    override suspend fun snapshot(): AccessResult<SessionSnapshot> {
        if (!initialized) {
            return AccessResult.Success(
                SessionSnapshot(
                    phase = "declared",
                    capabilities = mapOf("pageCache" to PageCacheDescriptor(emptyList(), 16, false, 0u)),
                ),
            )
        }
        return page.snapshot()
    }

    override suspend fun shutdown(): AccessResult<ShutdownReceipt> {
        val down = page.shutdown()
        native.close()
        initialized = false
        return down
    }
}
