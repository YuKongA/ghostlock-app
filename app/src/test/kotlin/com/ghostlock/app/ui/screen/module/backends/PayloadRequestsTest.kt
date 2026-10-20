package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PayloadRequestsTest {
    @Test
    fun followsPayloadOrder() {
        val state = FinishState(
            payloads = listOf(
                PayloadSlot("a", PayloadKind.Lkm, lkmUri = "content://custom.ko"),
                PayloadSlot("b", PayloadKind.Root),
                PayloadSlot("c", PayloadKind.Command, command = "id"),
                PayloadSlot("d", PayloadKind.Command, command = "uname -a"),
            ),
        )
        val context = PayloadContext(state, null, staged = mapOf("a" to "/tmp/custom.ko"))
        assertEquals(listOf("a", "b", "c", "d"), state.payloadRequests(context).map { it.slot.id })
    }

    @Test
    fun keepsDuplicateKindsInTableOrder() {
        val state = FinishState(
            payloads = listOf(
                PayloadSlot("c1", PayloadKind.Command, command = "id"),
                PayloadSlot("c2", PayloadKind.Command, command = "uname -a"),
            ),
        )
        val context = PayloadContext(state, null)
        assertEquals(listOf("c1", "c2"), state.payloadRequests(context).map { it.slot.id })
    }

    @Test
    fun completenessRules() {
        assertTrue(PayloadSlot("r", PayloadKind.Root).isComplete())
        assertFalse(PayloadSlot("c", PayloadKind.Command).isComplete())
        assertTrue(PayloadSlot("c", PayloadKind.Command, command = "id").isComplete())
        assertFalse(PayloadSlot("c", PayloadKind.Command, scriptMode = 1).isComplete())
        assertTrue(PayloadSlot("c", PayloadKind.Command, scriptMode = 1, scriptUri = "content://x").isComplete())
        assertFalse(PayloadSlot("l", PayloadKind.Lkm).isComplete())
        assertTrue(PayloadSlot("l", PayloadKind.Lkm, lkmUri = "content://k").isComplete())
    }

    @Test
    fun folkPatchAutoModuleLoadsWhenLkmSlotHasNoUri() {
        val state = FinishState(
            rootManager = "FolkPatch",
            payloads = listOf(
                PayloadSlot("root", PayloadKind.Root),
                PayloadSlot("lkm", PayloadKind.Lkm),
            ),
        )
        val context = PayloadContext(state, null, folkPatchLkm = "/data/local/tmp/kernelpatch.ko")
        assertEquals(listOf("root", "slot-folk"), state.payloadRequests(context).map { it.slot.id })
    }

    @Test
    fun folkPatchAutoModuleIsSuppressedByConfiguredLkmSlot() {
        val state = FinishState(
            rootManager = "FolkPatch",
            payloads = listOf(
                PayloadSlot("root", PayloadKind.Root),
                PayloadSlot("lkm", PayloadKind.Lkm, lkmUri = "content://k"),
            ),
        )
        val context = PayloadContext(
            state,
            null,
            staged = mapOf("lkm" to "/data/x/k.ko"),
            folkPatchLkm = "/data/local/tmp/kernelpatch.ko",
        )
        assertEquals(listOf("root", "lkm"), state.payloadRequests(context).map { it.slot.id })
    }

    @Test
    fun skipsDisabledSlots() {
        val state = FinishState(
            payloads = listOf(
                PayloadSlot("on", PayloadKind.Command, command = "id"),
                PayloadSlot("off", PayloadKind.Command, enabled = false, command = "reboot"),
            ),
        )
        val context = PayloadContext(state, null)
        assertEquals(listOf("on"), state.payloadRequests(context).map { it.slot.id })
    }

    @Test
    fun dropsPayloadsWithoutContent() {
        val state = FinishState(
            payloads = listOf(
                PayloadSlot("cmd", PayloadKind.Command),
                PayloadSlot("lkm", PayloadKind.Lkm),
            ),
        )
        val context = PayloadContext(state, null)
        assertEquals(emptyList<String>(), state.payloadRequests(context).map { it.slot.id })
    }
}
