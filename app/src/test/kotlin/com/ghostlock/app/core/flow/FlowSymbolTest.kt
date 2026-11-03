package com.ghostlock.app.core.flow

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** B1：`FlowSymbol`/`FlowSymbols.parse` 严格语法矩阵。 */
class FlowSymbolTest {
    @Test
    fun `parses canonical symbol`() {
        val symbol = FlowSymbols.parse("com.ghostlock.app.core.flow.ValidFlow#entry")
        assertEquals("com.ghostlock.app.core.flow.ValidFlow#entry", symbol?.canonical)
    }

    @Test
    fun `parses nested class with dollar`() {
        val raw = "com.ghostlock.app.core.flow.Outer\$Inner#entry"
        assertEquals(raw, FlowSymbols.parse(raw)?.canonical)
    }

    @Test
    fun `rejects invalid matrix`() {
        val invalid = listOf(
            "",
            "   ",
            "com.example.Flow",
            "com.example.Flow#",
            "#entry",
            "com.example.Flow#entry#extra",
            "com.example.Flow#entry extra",
            "com.example.Flow #entry",
            "com.example.Flow# entry",
            "com.exa mple.Flow#entry",
            "com.example.Flow#entries()",
            "com.example.Flow#entry#",
            "类.示例#入口",
            "com.example.类#entry",
        )
        for (raw in invalid) {
            assertNull("must reject: <$raw>", FlowSymbols.parse(raw))
        }
    }

    @Test
    fun `constructor rejects invalid parts`() {
        val bad = listOf(
            FlowSymbol::class.java.let { _ -> "bad class" to "entry" },
            "com.example.Flow" to "bad method",
            "com.example..Flow" to "entry",
            "com.example.Flow" to "entry()",
        )
        for ((className, methodName) in bad) {
            val failed = runCatching { FlowSymbol(className, methodName) }.isFailure
            assertTrue("must throw for <$className#$methodName>", failed)
        }
    }

    @Test
    fun `canonical roundtrip`() {
        val symbol = FlowSymbol("com.example.Flow", "entry")
        val reparsed = FlowSymbols.parse(symbol.canonical)
        assertEquals(symbol, reparsed)
        assertFalse(symbol.canonical.contains(' '))
    }
}
