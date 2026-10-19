package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertArrayEquals
import org.junit.Test

class RawEspWriterIvTest {
    @Test
    fun ivIsEcbDecryptXorDesired() {
        val key = ByteArray(32)
        val old = ByteArray(16)
        val desired = ByteArray(16) { 0x11 }
        val iv = RawEspWriter.computeIv(key, old, desired)
        val expected = byteArrayOf(
            0x76.toByte(), 0x76.toByte(), 0x0d.toByte(), 0xf0.toByte(),
            0xeb.toByte(), 0x80.toByte(), 0xcc.toByte(), 0xfa.toByte(),
            0x1e.toByte(), 0x9e.toByte(), 0xaa.toByte(), 0xa2.toByte(),
            0x77.toByte(), 0xa4.toByte(), 0x20.toByte(), 0xa5.toByte(),
        )
        assertArrayEquals(expected, iv)
    }
}
