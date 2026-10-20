package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.write.BlockAccess
import org.junit.Assert.assertTrue
import org.junit.Test

class RestoreLedgerTest {
    private class Fake : BlockAccess {
        val blocks = mutableMapOf<Long, ByteArray>()
        override fun writeBlock(path: String, offset: Long, desired: ByteArray, key: ByteArray, sequence: Int): Boolean {
            blocks[offset] = desired.copyOf(); return true
        }
        override fun writeRange(path: String, offset: Long, image: ByteArray, key: ByteArray, firstSequence: Int): Int {
            var at = 0
            while (at < image.size) { blocks[offset + at] = image.copyOfRange(at, at + 16); at += 16 }
            return image.size / 16
        }
        override fun readBlock(path: String, offset: Long): ByteArray? = blocks[offset]
    }

    @Test
    fun restoresRecordedBytes() {
        val fake = Fake()
        val ledger = RestoreLedger()
        ledger.record("x", 0, ByteArray(16) { 0x11 })
        assertTrue(ledger.restore(fake, ByteArray(32), 1))
        assertTrue(fake.blocks[0L]!!.contentEquals(ByteArray(16) { 0x11 }))
    }
}
