package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.access.EffectState
import com.ghostlock.app.core.access.ImageWriteReceipt
import com.ghostlock.app.core.access.PageCacheAccess
import com.ghostlock.app.core.access.PageCacheDescriptor
import com.ghostlock.app.core.access.PageCacheTarget
import com.ghostlock.app.core.access.WriteReceipt
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Kotlin writer：逐块写、直读优先、不可读退回 access.read。 */
class KotlinPageCacheWriterTest {

    private class FakeAccess(private val writable: Boolean = true) : PageCacheAccess {
        val blocks = linkedMapOf<Long, ByteArray>()
        override val descriptor = PageCacheDescriptor(listOf(Target), 16, true, 1u)
        override suspend fun targets(): AccessResult<List<PageCacheTarget>> = AccessResult.Success(listOf(Target))
        override suspend fun read(target: PageCacheTarget, offset: Long, bytes: Int): AccessResult<ByteArray> {
            val block = blocks[offset] ?: return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "missing")
            return AccessResult.Success(block)
        }
        override suspend fun write(target: PageCacheTarget, offset: Long, bytes: ByteArray): AccessResult<WriteReceipt> {
            if (!writable) return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "write-refused")
            blocks[offset] = bytes.copyOf()
            return AccessResult.Success(WriteReceipt(bytes.size, EffectState.CONFIRMED, 1u))
        }
        override suspend fun writeImage(
            target: PageCacheTarget, image: ByteArray, offset: Long, width: Int,
        ): AccessResult<ImageWriteReceipt> = AccessResult.Failure(AccessFailure.INVALID_REQUEST, "unused")
    }

    @Test
    fun writesAllBlocksAndVerifiesByRead() = runBlocking {
        val access = FakeAccess()
        val image = ByteArray(48) { index -> index.toByte() }
        val summary = KotlinPageCacheWriter.writeImage(access, Target, image, 0x100L)
        assertEquals(3, summary.written)
        assertEquals(3, summary.verified)
        assertTrue(summary.ok)
        assertEquals(0x100L, access.blocks.keys.first())
    }

    @Test
    fun directReadTakesPrecedence() = runBlocking {
        val access = FakeAccess()
        val image = ByteArray(16) { 0x22 }
        val summary = KotlinPageCacheWriter.writeImage(access, Target, image, 0L) { _, _ -> true }
        assertEquals(1, summary.written)
        assertEquals(1, summary.verified)
    }

    @Test
    fun refusedWriteStops() = runBlocking {
        val summary = KotlinPageCacheWriter.writeImage(FakeAccess(writable = false), Target, ByteArray(16), 0L)
        assertEquals(0, summary.written)
        assertEquals(0L, summary.failedAt)
        assertTrue(!summary.ok)
    }

    private companion object {
        val Target = PageCacheTarget("hook", "/system/lib64/libc++.so", true)
    }
}
