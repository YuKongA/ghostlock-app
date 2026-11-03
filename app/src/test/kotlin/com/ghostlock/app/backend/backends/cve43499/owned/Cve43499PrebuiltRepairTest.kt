package com.ghostlock.app.backend.backends.cve43499.owned

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * G05 prebuilt repair 状态机单测（task-70；先写不运行）。
 *
 * 覆盖：初始 EMPTY/discard clean（无 owner）；markRetained ⇒ RETAINED/discard 不伪报；
 * activation 非法状态返回 null；createdNanos 透传。
 */
class Cve43499PrebuiltRepairTest {

    @Test
    fun `empty prebuilt discards clean and reports created time`() {
        val prebuilt = Cve43499PrebuiltRepair(createdNanos = 1234L)
        assertEquals(1234L, prebuilt.createdNanos)
        assertEquals(Cve43499PrebuiltRepairState.EMPTY, prebuilt.currentState)
        assertFalse(prebuilt.available)
        assertFalse(prebuilt.retained)
        assertNull(prebuilt.takePageForActivation())
        val receipt = prebuilt.discard()
        assertTrue(receipt.clean)
        assertFalse(receipt.retained)
        assertEquals(Cve43499PrebuiltRepairState.DISCARDED, prebuilt.currentState)
        assertTrue(receipt.termination.contains("prebuilt=discarded"))
    }

    @Test
    fun `retained prebuilt never reports clean discard`() {
        val prebuilt = Cve43499PrebuiltRepair(createdNanos = 0L)
        prebuilt.markRetained("test-dirty")
        assertTrue(prebuilt.retained)
        assertEquals(Cve43499PrebuiltRepairState.RETAINED, prebuilt.currentState)
        val receipt = prebuilt.discard()
        assertFalse(receipt.clean)
        assertTrue(receipt.retained)
        assertTrue(prebuilt.retained)
        assertEquals("test-dirty", prebuilt.retainedReason)
    }

    @Test
    fun `activation without page returns null and does not fake state`() {
        val prebuilt = Cve43499PrebuiltRepair(createdNanos = 0L)
        assertNull(prebuilt.takePageForActivation())
        assertEquals(Cve43499PrebuiltRepairState.EMPTY, prebuilt.currentState)
        /* 幂等 discard：已 DISCARDED 不再伪报资源。 */
        assertTrue(prebuilt.discard().clean)
        assertTrue(prebuilt.discard().clean)
    }
}
