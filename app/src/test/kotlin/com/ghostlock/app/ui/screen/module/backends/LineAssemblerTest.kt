package com.ghostlock.app.ui.screen.module.backends

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** 行重组：分片、半行、flush。 */
class LineAssemblerTest {

    private fun feed(assembler: LineAssembler, text: String): List<ByteArray> =
        assembler.feed(text.toByteArray(), text.toByteArray().size)

    @Test
    fun emitsOnlyCompleteLines() {
        val assembler = LineAssembler()
        assertEquals(0, feed(assembler, "abc").size)
        val lines = feed(assembler, "def\nghi")
        assertEquals(1, lines.size)
        assertArrayEquals("abcdef\n".toByteArray(), lines[0])
        assertArrayEquals("ghi".toByteArray(), assembler.flush())
    }

    @Test
    fun splitsMultipleLines() {
        val assembler = LineAssembler()
        val lines = feed(assembler, "one\ntwo\n")
        assertEquals(2, lines.size)
        assertArrayEquals("one\n".toByteArray(), lines[0])
        assertArrayEquals("two\n".toByteArray(), lines[1])
        assertNull(assembler.flush())
    }
}
