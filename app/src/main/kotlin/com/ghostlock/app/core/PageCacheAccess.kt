package com.ghostlock.app.core

/** An opaque target resolved for exactly one session, not a raw caller-created file offset. */
class PageCacheTarget internal constructor(
    internal val ownerToken: Any,
    internal val nativeToken: Long,
    val offsetBytes: ULong,
)

/**
 * Session-bound page-cache operations, separate from persistent file writes.
 *
 * A successful page-cache receipt makes no claim about disk persistence or later cache state.
 */
interface PageCacheAccess : AccessCapability {
    /** Resolve a file target in this session; the backend must validate the returned token. */
    fun resolve(fileId: String, offsetBytes: ULong): AccessResult<PageCacheTarget>

    /** Read the requested cache bytes when the descriptor advertises a read limit. */
    fun read(target: PageCacheTarget, sizeBytes: Int): AccessResult<ByteArray>

    /** Attempt one constrained cache write and report its verified effect. */
    fun write(target: PageCacheTarget, bytes: ByteArray): AccessResult<WriteReceipt>
}
