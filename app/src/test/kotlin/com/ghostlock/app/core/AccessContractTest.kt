package com.ghostlock.app.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Host checks for capability limits and non-success write effects. */
class AccessContractTest {
    /** An exact-width operation must reject other widths and misaligned offsets. */
    @Test
    fun exactWidthAndAlignmentAreNotGeneralized() {
        val limits = OperationLimits(setOf(8), alignmentBytes = 8, repeatable = false)
        assertTrue(limits.permits(16uL, 8))
        assertFalse(limits.permits(16uL, 16))
        assertFalse(limits.permits(17uL, 8))
        assertFalse(limits.permits(ULong.MAX_VALUE, 8))
        assertFalse(limits.permits(0uL, 0))
    }

    /** An absent read remains absent even when the capability has a write operation. */
    @Test
    fun writeOnlyDescriptorDoesNotAdvertiseRead() {
        val descriptor = AccessDescriptor(
            backendId = "sample",
            domain = AccessDomain.FILE_PAGE_CACHE,
            read = null,
            write = OperationLimits(setOf(16), alignmentBytes = 1, repeatable = false),
        )
        assertEquals(null, descriptor.read)
        assertFalse(descriptor.write!!.permits(0uL, 8))
    }

    /** A verified receipt cannot encode zero bytes; unknown effects are typed failures. */
    @Test
    fun onlyVerifiedEffectGetsAReceipt() {
        assertEquals(8, WriteReceipt(8).verifiedBytes)
        assertEquals(AccessFailure.INDETERMINATE, AccessResult.Failure(AccessFailure.INDETERMINATE, "unknown").reason)
    }

    /** A caller mutation cannot alter the frame captured at request construction. */
    @Test
    fun requestCopiesProfileFrame() {
        val input = byteArrayOf(1)
        val request = AccessSessionRequest("sample", input)
        input[0] = 2
        val outgoing = request.consumeProfileFrame()
        assertEquals(1, outgoing[0].toInt())
        outgoing[0] = 3
        assertThrows(IllegalStateException::class.java) { request.consumeProfileFrame() }
    }

    /** A mutable caller set cannot change the native-authoritative snapshot. */
    @Test
    fun limitsCopyCallerSet() {
        val widths = mutableSetOf(8)
        val limits = OperationLimits(widths, alignmentBytes = 1, repeatable = false)
        widths += 16
        assertFalse(limits.permits(0uL, 16))
        assertThrows(UnsupportedOperationException::class.java) {
            (limits.widthsBytes as MutableSet<Int>).add(32)
        }
    }

    /** Invalid scalar extremes fail at construction rather than at native use. */
    @Test
    fun invalidDescriptorScalarsAreRejected() {
        assertThrows(IllegalArgumentException::class.java) { OperationLimits(setOf(0), 1, false) }
        assertThrows(IllegalArgumentException::class.java) { OperationLimits(setOf(8), 0, false) }
        assertThrows(IllegalArgumentException::class.java) { WriteReceipt(0) }
        assertThrows(IllegalArgumentException::class.java) {
            AccessDescriptor(" ", AccessDomain.KERNEL_VIRTUAL, null, OperationLimits(setOf(8), 1, false))
        }
    }
}
