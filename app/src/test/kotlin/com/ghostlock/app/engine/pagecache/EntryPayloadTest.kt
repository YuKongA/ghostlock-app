package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertTrue
import org.junit.Test

class EntryPayloadTest {
    @Test
    fun stringsArePresent() {
        val bytes = EntryPayload.bytes()
        val text = String(bytes, Charsets.ISO_8859_1)
        assertTrue(text.contains("/proc/self/attr/exec"))
        assertTrue(text.contains("u:r:vendor_modprobe:s0"))
        assertTrue(text.contains("/vendor/bin/insmod"))
        assertTrue(text.contains("/vendor/lib64/libbinderdebug.so"))
    }
}
