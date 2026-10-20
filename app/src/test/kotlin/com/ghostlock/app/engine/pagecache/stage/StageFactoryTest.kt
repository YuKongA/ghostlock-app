package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.KotlinSentryPlan
import com.ghostlock.app.engine.pagecache.write.BlockAccess
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test

class StageFactoryTest {
    private class FakeAccess : BlockAccess {
        val blocks = mutableMapOf<Long, ByteArray>()
        override fun writeBlock(path: String, offset: Long, desired: ByteArray, key: ByteArray, sequence: Int): Boolean {
            blocks[offset] = desired.copyOf(); return true
        }
        override fun writeRange(path: String, offset: Long, image: ByteArray, key: ByteArray, firstSequence: Int): Int {
            var at = 0
            while (at < image.size) { blocks[offset + at] = image.copyOfRange(at, at + 16); at += 16 }
            return image.size / 16
        }
        override fun readBlock(path: String, offset: Long): ByteArray? = blocks.getOrPut(offset) { ByteArray(16) }
    }

    @Test
    fun buildsAllStagesWithFakeAccess() = runBlocking {
        val access = FakeAccess()
        val spec = WriteSpec("x", ByteArray(16), ByteArray(32), 1, verify = { true })
        val hook = HookSpec(
            "x",
            KotlinSentryPlan(
                siteFile = 0x10, siteVaddr = 0x10, landingFile = 0, landingVaddr = 0,
                payloadMax = 4096, displaced = 0x11111111, guard = 0,
                branchWord = 0x14000002, jumpBackWord = 0,
            ),
            ByteArray(16), ByteArray(32), 1,
        ) { true }
        val engine = StageFactory.build(
            StageConfig(
                EnvironmentInput("android13-5.15", "carrier", 1, ByteArray(32), ByteArray(32)),
                access, spec, spec, hook, listOf("nonexistent-marker"), { true }, { true }, 50,
            ),
        )
        val results = engine.run()
        assertEquals(StageCode.WAIT_TIMEOUT, results[5].code)
        assertEquals(StageCode.OK, results.last().code)
    }
}
