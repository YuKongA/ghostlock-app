package com.ghostlock.app.backend.core

import org.junit.Assert.assertEquals
import org.junit.Test

/** Verifies bounded AIDL log framing, including Unicode split across chunks. */
class Utf8LogLineAssemblerTest {
    @Test
    fun preservesMultibyteCharactersSplitAcrossChunks() {
        val lines = mutableListOf<String>()
        val assembler = Utf8LogLineAssembler(lines::add)
        val bytes = "first\n猫尾\nlast".toByteArray(Charsets.UTF_8)
        assembler.append(bytes.copyOfRange(0, 7))
        assembler.append(bytes.copyOfRange(7, bytes.size))
        assembler.finish()

        assertEquals(listOf("first", "猫尾", "last"), lines)
    }

    @Test
    fun normalizesCarriageReturnsAndFinishesOnce() {
        val lines = mutableListOf<String>()
        val assembler = Utf8LogLineAssembler(lines::add)
        assembler.append("one\r\ntwo\nterminal\r".toByteArray(Charsets.UTF_8))
        assembler.finish()
        assembler.finish()

        assertEquals(listOf("one", "two", "terminal"), lines)
    }

    @Test(expected = IllegalStateException::class)
    fun rejectsBytesAfterFinish() {
        val assembler = Utf8LogLineAssembler { }
        assembler.finish()
        assembler.append("late".toByteArray(Charsets.UTF_8))
    }
}
