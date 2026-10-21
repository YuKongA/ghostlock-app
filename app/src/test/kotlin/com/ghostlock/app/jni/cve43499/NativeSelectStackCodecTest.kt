package com.ghostlock.app.jni.cve43499

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeSelectStackCodecTest {
    private fun params() = NativeSelectStackParams(
        target = 0x1000, value = 0x2000, fdCount = 320, attempts = 2,
        selectWaitUs = 250000, niceModulus = 4, waiterShift = -2, compact = true,
        fakeWaiterPrio = 140, fakeTask = 0x3000, fakeLock = 0x4000,
        wordTask = 6, wordLock = 7, wordTreePc = 2, wordTreeRight = 3, wordTreeLeft = 4,
        wordPiPc = 5, wordPiRight = 6, wordPiLeft = 7, wordWakePrio = 10,
        wordDeadline = 11, wordWwCtx = 12, waitWord = 0x5000, chainWord = 0x6000,
        targetWord = 0x7000, selected = intArrayOf(0, 1, 63),
    )

    @Test
    fun encodesFixedFieldsAndSelected() {
        val bytes = params().encode()
        val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(NativeSelectStackParams.HEADER + 12, bytes.size)
        assertEquals(NativeSelectStackParams.MAGIC, b.getInt(0))
        assertEquals(0x1000L, b.getLong(32))
        assertEquals(320, b.getInt(48))
        assertEquals(3, b.getInt(176))
        assertEquals(63, b.getInt(192 + 8))
    }

    @Test
    fun decodesResultAndRejectsBadHeader() {
        val b = ByteBuffer.allocate(128).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, NativeSelectStackResult.RESULT_MAGIC); b.putInt(8, 128)
        b.putInt(16, 0); b.putInt(32, 1); b.putInt(28, 2)
        val result = NativeSelectStackResult.decode(b.array())
        assertTrue(result!!.ok)
        assertEquals(2, result.calls)
        assertTrue(NativeSelectStackResult.decode(ByteArray(4)) == null)
    }
}
