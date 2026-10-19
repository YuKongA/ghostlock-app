package com.ghostlock.app.core.rootchannel

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class RootChannelProtocolTest {
    @Test
    fun requestRoundTrip() {
        val request = RootChannelProtocol.Request("echo hello", byteArrayOf(1, 2, 3))
        val decoded = RootChannelProtocol.decodeRequest(request.encode())!!
        assertEquals("echo hello", decoded.command)
        assertArrayEquals(byteArrayOf(1, 2, 3), decoded.stdin)
    }

    @Test
    fun exitFrameCarriesCode() {
        val frame = ByteArray(1 + 4 + 4).also {
            val buffer = RootChannelProtocol.encodeExit(7)
            buffer.get(it)
        }
        val type = frame[0].toInt()
        assertEquals(RootChannelProtocol.TYPE_EXIT, type)
        assertEquals(7, RootChannelProtocol.Frame(type, frame.copyOfRange(5, 9)).exitCode)
    }

    @Test
    fun rejectsBadMagic() {
        val buffer = java.nio.ByteBuffer.allocate(8).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0)
        buffer.putInt(0)
        buffer.flip()
        assertNull(RootChannelProtocol.decodeRequest(buffer))
    }
}
