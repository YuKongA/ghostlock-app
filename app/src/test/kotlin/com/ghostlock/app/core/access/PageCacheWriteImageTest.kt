package com.ghostlock.app.core.access

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PageCacheWriteImageTest {
    private val target = PageCacheTarget("carrier", "/tmp/carrier", writable = true)

    @Test
    fun splitsImageIntoCapabilityWrites() = runBlocking {
        val writes = mutableListOf<Int>()
        val ops = object : PageCacheOps {
            override fun up() = true
            override fun targets() = listOf(target)
            override fun readBlock(target: PageCacheTarget, offset: Long, out: ByteArray) = true
            override fun writeBlock(target: PageCacheTarget, offset: Long, block: ByteArray): Boolean {
                writes += block.size
                return true
            }
            override fun tearDown() = true
        }
        val session = PageCacheKernelAccess(ops)
        session.initialize()
        val access = session.pageCache
        val receipt = access.writeImage(target, ByteArray(32) { 0x41 }, 0L, 16)
        assertTrue(receipt is AccessResult.Success)
        assertEquals(32, (receipt as AccessResult.Success).value.bytes)
        assertEquals(listOf(16, 16), writes)
    }

    @Test
    fun rejectsImageNotAlignedToWidth() = runBlocking {
        val ops = object : PageCacheOps {
            override fun up() = true
            override fun targets() = listOf(target)
            override fun readBlock(target: PageCacheTarget, offset: Long, out: ByteArray) = true
            override fun writeBlock(target: PageCacheTarget, offset: Long, block: ByteArray) = true
            override fun tearDown() = true
        }
        val session = PageCacheKernelAccess(ops)
        session.initialize()
        val result = session.pageCache.writeImage(target, ByteArray(17), 0L, 16)
        assertTrue(result is AccessResult.Failure)
        assertEquals(AccessFailure.INVALID_REQUEST, (result as AccessResult.Failure).reason)
    }
}
