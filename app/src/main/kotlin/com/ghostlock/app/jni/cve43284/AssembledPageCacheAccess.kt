package com.ghostlock.app.jni.cve43284

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.access.EffectState
import com.ghostlock.app.core.access.ImageWriteReceipt
import com.ghostlock.app.core.access.PageCacheAccess
import com.ghostlock.app.core.access.PageCacheDescriptor
import com.ghostlock.app.core.access.PageCacheTarget
import com.ghostlock.app.core.access.WriteReceipt

/**
 * 43284 业务数据面的 PageCacheAccess 实现。
 *
 * 底层仍是同一个 Assembled43284 会话（accessId / sequence / 回读校验），
 * 但业务层只通过本接口写镜像和块，不再直接调用 Native43284.writeImage。
 */
class AssembledPageCacheAccess(
    private val assembled: Assembled43284,
    private val target: PageCacheTarget,
    private val init: Native43284.SessionParams43284?,
    private val outcome: LongArray? = null,
    ticket: Long = -1L,
) : PageCacheAccess {
    /** 块读写用的 ticket；缺省沿用 accessId（旧行为）。 */
    private val blockTicket: Long = if (ticket > 0L) ticket else assembled.accessId
    override val descriptor: PageCacheDescriptor = PageCacheDescriptor(
        targets = listOf(target),
        blockWidth = Assembled43284.BLOCK,
        writable = true,
        generation = 1u,
    )

    override suspend fun targets(): AccessResult<List<PageCacheTarget>> = AccessResult.Success(listOf(target))

    override suspend fun read(target: PageCacheTarget, offset: Long, bytes: Int): AccessResult<ByteArray> {
        if (target.id != this.target.id) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "unknown target")
        }
        if (bytes != Assembled43284.BLOCK) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "block width is " + Assembled43284.BLOCK)
        }
        val block = assembled.readBlockNative(blockTicket, offset)
            ?: return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, assembled.lastRefusal)
        return AccessResult.Success(block)
    }

    override suspend fun write(target: PageCacheTarget, offset: Long, bytes: ByteArray): AccessResult<WriteReceipt> {
        if (target.id != this.target.id) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "unknown target")
        }
        if (bytes.size != Assembled43284.BLOCK) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "block width is " + Assembled43284.BLOCK)
        }
        val code = assembled.writeBlockNative(blockTicket, offset, bytes)
        if (code != 0) {
            return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, assembled.lastRefusal)
        }
        return AccessResult.Success(WriteReceipt(bytes.size, EffectState.CONFIRMED, descriptor.generation))
    }

    override suspend fun writeImage(
        target: PageCacheTarget,
        image: ByteArray,
        offset: Long,
        width: Int,
    ): AccessResult<ImageWriteReceipt> {
        if (target.id != this.target.id) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "unknown target")
        }
        if (width != Assembled43284.BLOCK || image.isEmpty() || image.size % width != 0) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "image width or size is invalid")
        }
        val session = init ?: return AccessResult.Failure(AccessFailure.NOT_READY, "image init is missing")
        val ok = assembled.writeImageNative(image, offset, width.toLong(), session, outcome)
        if (!ok) {
            return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, assembled.lastRefusal)
        }
        return AccessResult.Success(
            ImageWriteReceipt(
                bytes = image.size,
                effect = EffectState.CONFIRMED,
                generation = descriptor.generation,
                sequence = assembled.sequenceNow(),
            ),
        )
    }
}
