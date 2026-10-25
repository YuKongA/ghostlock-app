package com.ghostlock.app.core.access

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class AccessIoTest {
    @Test
    fun `zero length read is data and is not validated here`() {
        val request = KernelReadRequest(address = 1UL, length = 0)
        assertEquals(1UL, request.address)
        assertEquals(0, request.length)
    }

    @Test
    fun `empty page cache write is data and is not validated here`() {
        val bytes = byteArrayOf()
        val request = PageCacheWriteRequest(offset = 0UL, bytes = bytes)
        assertEquals(0UL, request.offset)
        assertArrayEquals(bytes, request.bytes)
    }

    @Test
    fun `write receipt only describes bytes written`() {
        assertEquals(8, AccessWriteReceipt(bytesWritten = 8).bytesWritten)
    }
}
