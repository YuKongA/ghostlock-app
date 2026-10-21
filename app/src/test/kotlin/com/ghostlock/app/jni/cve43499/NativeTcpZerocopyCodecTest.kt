package com.ghostlock.app.jni.cve43499

import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeTcpZerocopyCodecTest {
    @Test
    fun decodesStatusAndCleanup() {
        val b = ByteBuffer.allocate(NativeTcpZerocopyResult.BYTES).order(ByteOrder.LITTLE_ENDIAN)
        b.putInt(0, 0); b.putInt(4, 0); b.putInt(8, 0); b.putInt(12, 1); b.putInt(16, 1)
        val ok = NativeTcpZerocopyResult.decode(b.array())
        assertTrue(ok!!.ok)
        assertEquals(1, ok.userspaceClean)

        b.putInt(0, 2)
        assertFalse(NativeTcpZerocopyResult.decode(b.array())!!.ok)
        assertTrue(NativeTcpZerocopyResult.decode(ByteArray(4)) == null)
    }
}
