package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.KotlinSentryPlan
import com.ghostlock.app.engine.pagecache.write.BlockAccess
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HookStageTest {
    private class FakeAccess : BlockAccess {
        val blocks = mutableMapOf<Long, ByteArray>()
        override fun writeBlock(path: String, offset: Long, desired: ByteArray, key: ByteArray, sequence: Int): Boolean {
            blocks[offset] = desired.copyOf()
            return true
        }
        override fun writeRange(path: String, offset: Long, image: ByteArray, key: ByteArray, firstSequence: Int): Int {
            var at = 0
            while (at < image.size) {
                blocks[offset + at] = image.copyOfRange(at, at + 16)
                at += 16
            }
            return image.size / 16
        }
        override fun readBlock(path: String, offset: Long): ByteArray? =
            blocks.getOrPut(offset) { ByteArray(16) }
    }

    @Test
    fun writesPayloadThenSiteBranch() = runBlocking {
        val access = FakeAccess()
        val plan = KotlinSentryPlan(
            siteFile = 0x10, siteVaddr = 0x10, landingFile = 0, landingVaddr = 0,
            payloadMax = 4096, displaced = 0x11111111, guard = 0,
            branchWord = 0x14000002, jumpBackWord = 0,
        )
        val stage = HookStage(access, "x", plan, ByteArray(16) { 0x22 }, ByteArray(32), 1) { true }
        assertEquals(StageCode.OK, stage.run().code)
        val site = access.blocks[0x10]!!
        assertEquals(0x02, site[0].toInt() and 0xFF)
        assertEquals(0x00, site[1].toInt() and 0xFF)
        assertEquals(0x00, site[2].toInt() and 0xFF)
        assertEquals(0x14, site[3].toInt() and 0xFF)
        assertTrue(access.blocks.containsKey(0L))
    }
}
