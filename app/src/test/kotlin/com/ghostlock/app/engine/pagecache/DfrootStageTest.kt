package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DfrootStageTest {
    @Test
    fun stageLayoutMatchesDfroot() {
        val stage = DfrootLibcxx.bytes().copyOf(464)
        assertEquals(464, stage.size)
        assertEquals(0x90, DfrootLibcxx.ENTRY_OFFSET)
        assertEquals(0x1C8, DfrootLibcxx.FIRST_INST_OFFSET)
        assertEquals(0x1CC, DfrootLibcxx.JUMP_BACK_OFFSET)
        assertTrue(stage.size % 16 == 0)

        val site = 0x6EB54L
        val landing = 0x9A160L
        val first = 0xA9BE7BFD.toInt()
        for (index in 0 until 4) {
            stage[DfrootLibcxx.FIRST_INST_OFFSET + index] =
                ((first ushr (index * 8)) and 0xFF).toByte()
        }
        val jump = KotlinElfPlanner.encodeBranch(
            landing + DfrootLibcxx.JUMP_BACK_OFFSET,
            site + 4,
        )
        assertEquals(0x17FF520B, jump)
        for (index in 0 until 4) {
            stage[DfrootLibcxx.JUMP_BACK_OFFSET + index] =
                (((jump!! ushr (index * 8)) and 0xFF)).toByte()
        }
        assertEquals(0xFD, stage[DfrootLibcxx.FIRST_INST_OFFSET].toInt() and 0xFF)
        assertEquals(0x0B, stage[DfrootLibcxx.JUMP_BACK_OFFSET].toInt() and 0xFF)
    }
}
