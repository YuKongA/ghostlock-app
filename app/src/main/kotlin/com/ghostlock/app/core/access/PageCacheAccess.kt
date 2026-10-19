package com.ghostlock.app.core.access

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * 页缓存底层操作（JNI 边界只提供这些原语；Kotlin 负责编排与校验）。
 *
 * 单位是 [PageCacheKernelAccess] 的 blockWidth，与 native 43284 page-cache landing 一致。
 */
interface PageCacheOps {
    fun up(): Boolean
    fun targets(): List<PageCacheTarget>
    fun readBlock(target: PageCacheTarget, offset: Long, out: ByteArray): Boolean
    fun writeBlock(target: PageCacheTarget, offset: Long, block: ByteArray): Boolean
    fun tearDown(): Boolean
}

/**
 * 页缓存会话：Kotlin 编排，资源由 [PageCacheOps] 的 native 所有者负责回收。
 *
 * 能力视图借用本会话，不单独 close；shutdown 只调用 ops.tearDown()。
 */
class PageCacheKernelAccess(
    private val ops: PageCacheOps,
    private val blockWidth: Int = 16,
) : AccessSession, AccessCapabilities {
    private val gate = Mutex()
    private var generation: UInt = 0u
    private var ready = false

    private val pageView = object : PageCacheAccess {
        override val descriptor: PageCacheDescriptor
            get() = descriptorNow()

        override suspend fun targets(): AccessResult<List<PageCacheTarget>> = gate.withLock {
            if (!ready) {
                AccessResult.Failure(AccessFailure.NOT_READY, "session not ready")
            } else {
                AccessResult.Success(ops.targets())
            }
        }

        override suspend fun read(
            target: PageCacheTarget,
            offset: Long,
            bytes: Int,
        ): AccessResult<ByteArray> = gate.withLock {
            if (!ready) {
                return@withLock AccessResult.Failure(AccessFailure.NOT_READY, "session not ready")
            }
            if (bytes != blockWidth) {
                return@withLock AccessResult.Failure(AccessFailure.INVALID_REQUEST, "block width is " + blockWidth)
            }
            if (!descriptorNow().targets.any { it.id == target.id }) {
                return@withLock AccessResult.Failure(AccessFailure.INVALID_REQUEST, "unknown target " + target.id)
            }
            val out = ByteArray(blockWidth)
            if (!ops.readBlock(target, offset, out)) {
                return@withLock AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "read_block failed")
            }
            AccessResult.Success(out)
        }

        override suspend fun write(
            target: PageCacheTarget,
            offset: Long,
            bytes: ByteArray,
        ): AccessResult<WriteReceipt> = gate.withLock {
            if (!ready) {
                return@withLock AccessResult.Failure(AccessFailure.NOT_READY, "session not ready")
            }
            if (bytes.size != blockWidth) {
                return@withLock AccessResult.Failure(AccessFailure.INVALID_REQUEST, "block width is " + blockWidth)
            }
            if (!descriptorNow().targets.any { it.id == target.id }) {
                return@withLock AccessResult.Failure(AccessFailure.INVALID_REQUEST, "unknown target " + target.id)
            }
            val written = ops.writeBlock(target, offset, bytes.copyOf())
            val receipt = WriteReceipt(
                bytes = bytes.size,
                effect = if (written) EffectState.CONFIRMED else EffectState.UNKNOWN,
                generation = generation,
            )
            AccessResult.Success(receipt)
        }
    }

    override val pageCache: PageCacheAccess get() = pageView
    override val kernelMemory: KernelMemoryAccess? get() = null

    override val capabilities: AccessCapabilities get() = this

    private fun descriptorNow(): PageCacheDescriptor = PageCacheDescriptor(
        targets = if (ready) ops.targets() else emptyList(),
        blockWidth = blockWidth,
        writable = ready,
        generation = generation,
    )

    override suspend fun snapshot(): AccessResult<SessionSnapshot> = gate.withLock {
        AccessResult.Success(
            SessionSnapshot(
                phase = if (ready) "ready" else "declared",
                capabilities = mapOf("pageCache" to descriptorNow()),
            ),
        )
    }

    override suspend fun initialize(): AccessResult<InitializationReceipt> = gate.withLock {
        if (!ops.up()) {
            return@withLock AccessResult.Failure(AccessFailure.NOT_READY, "page-cache landing is not up")
        }
        ready = true
        generation += 1u
        AccessResult.Success(InitializationReceipt(listOf(descriptorNow())))
    }

    override suspend fun shutdown(): AccessResult<ShutdownReceipt> = gate.withLock {
        if (!ready) {
            return@withLock AccessResult.Success(ShutdownReceipt(clean = true))
        }
        val clean = runCatching { ops.tearDown() }.getOrDefault(false)
        ready = false
        generation += 1u
        AccessResult.Success(ShutdownReceipt(clean = clean))
    }
}
