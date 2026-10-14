package com.ghostlock.app.core

/** The address domain in which an operation is meaningful. */
enum class AccessDomain { KERNEL_VIRTUAL, FILE_PAGE_CACHE }

/**
 * Limits for one operation; a null limit in [AccessDescriptor] means that operation is absent.
 *
 * @property widthsBytes exact supported transfer widths, never a promise of arbitrary length.
 * @property alignmentBytes required alignment of the target address or file offset.
 * @property repeatable whether a successful operation may be attempted again in this session.
 */
class OperationLimits(
    widthsBytes: Set<Int>,
    val alignmentBytes: Int,
    val repeatable: Boolean,
) {
    val widthsBytes: Set<Int> = java.util.Collections.unmodifiableSet(widthsBytes.toSet())

    init {
        require(this.widthsBytes.isNotEmpty() && this.widthsBytes.all { it > 0 })
        require(alignmentBytes > 0)
    }

    /**
     * Preliminary screening only. A true result does not establish target/value eligibility;
     * the backend must validate its full operation-specific constraints again.
     */
    fun permits(offset: ULong, widthBytes: Int): Boolean =
        widthBytes in widthsBytes && offset % alignmentBytes.toULong() == 0uL
}

/**
 * Native-authoritative capabilities for one ready session.
 *
 * A Kotlin caller may reject an operation using this descriptor, but native code must validate
 * it again. An absent read/write limit must never be inferred from the backend identifier.
 */
data class AccessDescriptor(
    val backendId: String,
    val domain: AccessDomain,
    val read: OperationLimits?,
    val write: OperationLimits?,
) {
    init {
        require(backendId.isNotBlank())
        require(read != null || write != null)
    }
}
