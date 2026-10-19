package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DfrootLibcxxTest {
    @Test
    fun sizeAndStrings() {
        val bytes = DfrootLibcxx.bytes()
        assertEquals(468, bytes.size)
        val text = String(bytes, Charsets.ISO_8859_1)
        assertTrue(text.contains("/dev/df"))
        assertTrue(text.contains("u:r:vendor_modprobe:s0"))
        assertTrue(text.contains("/vendor/bin/insmod"))
    }
}
