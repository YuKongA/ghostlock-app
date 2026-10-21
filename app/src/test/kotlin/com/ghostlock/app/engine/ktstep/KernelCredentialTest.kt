package com.ghostlock.app.engine.ktstep

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class KernelCredentialTest {
    private fun deviceLayout(): CredentialLayout = CredentialLayout(
        copySize = 176,
        usageOffset = 0,
        usageValue = 256,
        capsOffset = 48,
        capsCount = 3,
        capsValue = 2199023255551L,
        refOffsets = intArrayOf(128, 136, 144, 152),
        refImages = longArrayOf(
            18446743799011097216uL.toLong(),
            18446743799012843792uL.toLong(),
            18446743799011098608uL.toLong(),
            18446743799011097384uL.toLong(),
        ),
        refCount = 4,
    )

    private fun load32(buffer: ByteArray, at: Int): Long {
        var value = 0L
        for (index in 0 until 4) value = value or ((buffer[at + index].toLong() and 0xFFL) shl (8 * index))
        return value
    }

    private fun load64(buffer: ByteArray, at: Int): Long {
        var value = 0L
        for (index in 0 until 8) value = value or ((buffer[at + index].toLong() and 0xFFL) shl (8 * index))
        return value
    }

    @Test
    fun theDeviceCopyComesOut() {
        val layout = deviceLayout()
        val buffer = ByteArray(256) { 0xAA.toByte() }
        assertEquals(LayoutVerdict.Ok, buildCredentialImage(buffer, layout, 0))
        assertEquals(256L, load32(buffer, 0))
        assertEquals(layout.capsValue, load64(buffer, 48))
        assertEquals(layout.capsValue, load64(buffer, 64))
        assertEquals(layout.refImages[0], load64(buffer, 128))
        assertEquals(layout.refImages[3], load64(buffer, 152))
        assertEquals(0, buffer[16].toInt())
        assertEquals(0, buffer[175].toInt())
        assertEquals(0xAA, buffer[176].toInt() and 0xFF)

        val moved = ByteArray(512) { 0x55.toByte() }
        assertEquals(LayoutVerdict.Ok, buildCredentialImage(moved, layout, 64))
        assertEquals(256L, load32(moved, 64))
        assertEquals(0x55, moved[63].toInt() and 0xFF)
    }

    @Test
    fun impossibleLayoutsAreRefused() {
        val layout = deviceLayout()
        val buffer = ByteArray(256)
        assertEquals(LayoutVerdict.NoRoom, buildCredentialImage(ByteArray(100), layout, 0))
        assertEquals(LayoutVerdict.NoRoom, buildCredentialImage(buffer, layout, 200))
        assertEquals(LayoutVerdict.LayoutInvalid,
            buildCredentialImage(buffer, layout.copy(refOffsets = intArrayOf(128, 136, 144, 200)), 0))
        assertEquals(LayoutVerdict.TooManyReferences, buildCredentialImage(buffer, layout.copy(refCount = 5), 0))
        assertEquals(LayoutVerdict.LayoutInvalid, buildCredentialImage(buffer, layout.copy(copySize = 0), 0))
    }

    @Test
    fun refusalsWriteNothing() {
        val layout = deviceLayout().copy(refOffsets = intArrayOf(300, 136, 144, 152))
        val buffer = ByteArray(256) { 0xC3.toByte() }
        assertTrue(buildCredentialImage(buffer, layout, 0) != LayoutVerdict.Ok)
        assertTrue(buffer.all { it == 0xC3.toByte() })
        assertEquals(LayoutVerdict.LayoutInvalid, buildCredentialImage(ByteArray(0), deviceLayout(), 0))
    }
}
