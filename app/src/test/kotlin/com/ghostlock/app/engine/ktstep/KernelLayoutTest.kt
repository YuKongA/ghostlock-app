package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class KernelLayoutTest {
    private fun retiredInputs(): PayloadPageInputs = PayloadPageInputs(
        slots = PayloadSlots(0x0E80, 0x1180, 0x1280, 0x1080, 0x0F80),
        lock = LockFields(0x00, 0x08, 0x10, 0x18),
        waiter = WaiterFields(
            headWord = 0x00, headNext = 0x08, headPrev = 0x10, pc = 0x18, right = 0x20,
            left = 0x28, task = 0x30, lock = 0x38, wakeState = 0x40, prio = 0x44,
            deadline = 0x48, wwCtx = 0x50,
        ),
        task = TaskFields(
            usage = 0x40, prio = 0x84, normalPrio = 0x8C, piLock = 0x90C,
            piWaiters = 0x920, piTopTask = 0x930, piBlockedOn = 0x938, group = 0x348,
        ),
        values = PayloadValues(taskUsage = 0x100, taskPrio = 120, waiterPrio = 140),
        waiterWord = 0xFFFF800000100000uL.toLong() + 0x1180L,
        waiterTask = 0L,
        fakeTask = 0xFFFF800000100000uL.toLong() + 0x1280L,
        fakeLock = 0xFFFF800000100000uL.toLong() + 0x0E80L,
        taskGroup = 0L,
        piTopTask = 0L,
        pointerBytes = 8,
    )

    private fun wordAt(page: ByteArray, at: Int): Long {
        var value = 0L
        for (index in 0 until 8) {
            value = value or ((page[at + index].toLong() and 0xFFL) shl (8 * index))
        }
        return value
    }

    private fun halfAt(page: ByteArray, at: Int): Int {
        return (page[at].toInt() and 0xFF) or
            ((page[at + 1].toInt() and 0xFF) shl 8) or
            ((page[at + 2].toInt() and 0xFF) shl 16) or
            ((page[at + 3].toInt() and 0xFF) shl 24)
    }

    @Test
    fun aPreservingWriteLaysThePageOut() {
        val page = ByteArray(0x8000)
        val inputs = retiredInputs()
        val request = WriteRequest(
            target = 0xFFFFFFC000235EE0uL.toLong(),
            mode = WriteMode.Zero,
            preserveChild = true,
        )
        val layout = WriteLayout(
            parent = request.target - 8L,
            value = inputs.waiterWord + 0x100L,
            fops = 0L,
            needsCredentialCopy = false,
        )
        assertEquals(LayoutVerdict.Ok, buildPayloadPage(page, inputs, request, layout))
        assertEquals(0, halfAt(page, 0x0E80))
        assertEquals(inputs.waiterWord, wordAt(page, 0x0E88))
        assertEquals(inputs.waiterWord, wordAt(page, 0x0E90))
        assertEquals(inputs.fakeTask or 1L, wordAt(page, 0x0E98))
        assertEquals(1L, wordAt(page, 0x1180))
        assertEquals(0L, wordAt(page, 0x1188))
        assertEquals(0L, wordAt(page, 0x1190))
        assertEquals(layout.parent, wordAt(page, 0x1198))
        assertEquals(layout.parent, wordAt(page, 0x11A0))
        assertEquals(layout.parent, wordAt(page, 0x11A8))
        assertEquals(inputs.fakeTask, wordAt(page, 0x11B0))
        assertEquals(inputs.fakeLock, wordAt(page, 0x11B8))
        assertEquals(0, halfAt(page, 0x11C0))
        assertEquals(140, halfAt(page, 0x11C4))
        assertEquals(0L, wordAt(page, 0x11C8))
        assertEquals(0L, wordAt(page, 0x11D0))
        assertEquals(0x100, halfAt(page, 0x12C0))
        assertEquals(120, halfAt(page, 0x1304))
        assertEquals(120, halfAt(page, 0x130C))
        assertEquals(0, halfAt(page, 0x1280 + 0x90C))
        assertEquals(0L, wordAt(page, 0x1280 + 0x920))
        assertEquals(0L, wordAt(page, 0x1280 + 0x928))
        assertEquals(0L, wordAt(page, 0x1280 + 0x930))
        assertEquals(0L, wordAt(page, 0x1280 + 0x938))
    }

    @Test
    fun aLeafWriteCarriesNoValue() {
        val page = ByteArray(0x8000)
        val inputs = retiredInputs()
        val request = WriteRequest(
            target = 0xFFFFFFC000200000uL.toLong(),
            mode = WriteMode.Zero,
            preserveChild = false,
        )
        val layout = WriteLayout(request.target - 8L, 0L, 0L, false)
        assertEquals(LayoutVerdict.Ok, buildPayloadPage(page, inputs, request, layout))
        assertEquals(layout.parent, wordAt(page, 0x1198))
        assertEquals(layout.parent, wordAt(page, 0x11A0))
        assertEquals(layout.parent, wordAt(page, 0x11A8))
    }

    @Test
    fun refusalsLeaveThePageUntouched() {
        val page = ByteArray(0x8000) { 0xAA.toByte() }
        val inputs = retiredInputs()
        val request = WriteRequest(0x1000L, WriteMode.Zero, false)
        val layout = WriteLayout(0L, 0x2000L, 0L, false)
        assertEquals(LayoutVerdict.SlotsInvalid,
            buildPayloadPage(page, inputs.copy(slots = inputs.slots.copy(waiter = 0)), request, layout))
        assertEquals(LayoutVerdict.NoRoom, buildPayloadPage(ByteArray(0x1000), inputs, request, layout))
        assertEquals(LayoutVerdict.WidthInvalid,
            buildPayloadPage(page, inputs.copy(pointerBytes = 9), request, layout))
        assertEquals(LayoutVerdict.NoRoom, buildPayloadPage(ByteArray(0), inputs, request, layout))
        assertTrue(page.all { it == 0xAA.toByte() })
    }

    @Test
    fun layoutRulesMatchTheCppContract() {
        val inputs = LayoutInputs(
            pageValue = 0x10000010001L,
            credentialValue = 0xFFFFFF802ABFD588uL.toLong(),
            defaultFops = 0x10L,
            credentialFops = 0x20L,
            pointerBytes = 8,
        )
        val zero = WriteRequest(0x2000L, WriteMode.Zero, true)
        val zeroLayout = writeLayout(zero, inputs)
        assertEquals(0x2000L - 8L, zeroLayout.parent)
        assertTrue(layoutMatchesRequest(zero, zeroLayout))
        assertTrue(layoutAcceptsPage(zero, zeroLayout))
        val even = zeroLayout.copy(value = 0x10000000000L)
        assertTrue(!layoutAcceptsPage(zero, even))
        val credential = WriteRequest(0x3000L, WriteMode.Credential, true)
        val credentialLayout = writeLayout(credential, inputs)
        assertEquals(inputs.credentialValue, credentialLayout.value)
        assertEquals(inputs.credentialFops, credentialLayout.fops)
        assertTrue(credentialLayout.needsCredentialCopy)
        assertEquals(0L, armFor(WriteRequest(0x4000L, WriteMode.Zero, false), 0x55L, 8).right)
        assertEquals(0x55L, armFor(WriteRequest(0x4000L, WriteMode.Zero, true), 0x55L, 8).right)
        assertEquals(0x4000L - 8L, armFor(WriteRequest(0x4000L, WriteMode.Zero, true), 0x55L, 8).pc)
    }
}
