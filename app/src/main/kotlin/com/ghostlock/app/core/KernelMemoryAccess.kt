package com.ghostlock.app.core

/** A kernel virtual address, intentionally distinct from a file offset. */
@JvmInline
value class KernelAddress(val bits: ULong)

/**
 * Session-bound kernel-memory operations.
 *
 * The descriptor, not this interface's presence, determines whether read and/or write exists.
 * Each call returns an explicit unsupported result when its operation is absent.
 */
interface KernelMemoryAccess : AccessCapability {
    /** Read exactly [sizeBytes] from [address], or return a typed failure. */
    fun read(address: KernelAddress, sizeBytes: Int): AccessResult<ByteArray>

    /** Attempt one constrained write; the receipt records the observed effect. */
    fun write(address: KernelAddress, bytes: ByteArray): AccessResult<WriteReceipt>
}
