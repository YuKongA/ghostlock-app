package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Round-trip and syntax coverage for the configuration store. These are the
 * exact paths that broke before: rendering an offsets array back into a
 * document, null/empty groups, quoted keys and legacy JSON input.
 */
class HoconSupportTest {

    private fun Any?.asLong(): Long = (this as? Number)?.toLong() ?: error("not a number: $this")

    @Test
    fun `top level array keeps brackets and round trips`() {
        val entries = valueListOf(
            valueMapOf(
                "release" to "6.1.115-test",
                "recommend_shizuku" to 0,
                "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to 1)),
                "fallback" to valueMapOf(
                    "to" to "select_stack",
                    "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to 1)),
                ),
                "task_struct" to valueMapOf("prio" to 132),
                "execution" to valueMapOf("stages" to valueMapOf("w1_attempts" to 15)),
            ),
            valueMapOf(
                "release" to "5.15.189-test",
                "route" to valueMapOf("multicast_waiter" to valueMapOf("waiter_off" to 96)),
                "fallback" to valueMapOf("to" to "none"),
            ),
        )

        val text = HoconSupport.render(entries)
        assertTrue("top-level array needs brackets", text.trimStart().startsWith("["))

        val parsed = HoconSupport.parseValue(text).asValueList()
        assertEquals(2, parsed?.size)
        val first = parsed!![0].asValueMap()!!
        assertEquals("6.1.115-test", first["release"])
        assertEquals("tcp_zerocopy", first["route"].asValueMap()!!.keys.first())
        assertEquals("select_stack", first["fallback"].asValueMap()!!["to"])
        assertEquals(
            1L,
            first["fallback"].asValueMap()!!["route"].asValueMap()!!
                ["select_stack"].asValueMap()!!["waiter_shift"].asLong(),
        )
        assertEquals(132L, first["task_struct"].asValueMap()!!["prio"].asLong())
        assertEquals(15L, first["execution"].asValueMap()!!["stages"].asValueMap()!!["w1_attempts"].asLong())
        assertEquals("none", parsed[1].asValueMap()!!["fallback"].asValueMap()!!["to"])
    }

    @Test
    fun `object round trip keeps null empty group and empty array`() {
        val source = valueMapOf(
            "release" to "r",
            "nullable" to null,
            "emptyGroup" to valueMapOf(),
            "emptyList" to valueListOf(),
            "nested" to valueMapOf("deep" to valueMapOf("value" to 7)),
            "list" to valueListOf(1, 2),
        )

        val parsed = HoconSupport.parseValue(HoconSupport.render(source)).asValueMap()!!

        assertEquals("r", parsed["release"])
        assertTrue(parsed.containsKey("nullable"))
        assertEquals(null, parsed["nullable"])
        assertEquals(0, parsed["emptyGroup"].asValueMap()!!.size)
        assertEquals(0, parsed["emptyList"].asValueList()!!.size)
        assertEquals(7L, parsed["nested"].asValueMap()!!["deep"].asValueMap()!!["value"].asLong())
        assertEquals(2, parsed["list"].asValueList()!!.size)
    }

    @Test
    fun `special keys and escaped strings round trip`() {
        val source = valueMapOf(
            "a.b c" to "quoted key",
            "plain_key-1" to "value with \"quotes\" and \\backslash\nand newline",
            "unicode" to "中文值",
        )

        val parsed = HoconSupport.parseValue(HoconSupport.render(source)).asValueMap()!!

        assertEquals("quoted key", parsed["a.b c"])
        assertEquals("value with \"quotes\" and \\backslash\nand newline", parsed["plain_key-1"])
        assertEquals("中文值", parsed["unicode"])
    }

    @Test
    fun `hocon syntax is accepted`() {
        val parsed = HoconSupport.parseValue(
            """
            # comment
            release = "x"
            fallback { to = select_stack }
            list = [1, 2,]
            optional = ${'$'}{?NOT_DEFINED}
            """.trimIndent(),
        ).asValueMap()!!

        assertEquals("x", parsed["release"])
        assertEquals("select_stack", parsed["fallback"].asValueMap()!!["to"])
        assertEquals(2, parsed["list"].asValueList()!!.size)
        assertFalse(parsed.containsKey("optional"))
    }

    @Test
    fun `legacy json document parses`() {
        val parsed = HoconSupport.parseValue(
            """[{"release":"x","route":{"select_stack":{"waiter_shift":-2}}}]""",
        ).asValueList()!!

        assertEquals(1, parsed.size)
        assertEquals(
            -2L,
            parsed[0].asValueMap()!!["route"].asValueMap()!!
                ["select_stack"].asValueMap()!!["waiter_shift"].asLong(),
        )
    }

    @Test
    fun `value model round trips nested route and fallback`() {
        val map = valueMapOf(
            "release" to "6.1.115-test",
            "recommend_shizuku" to 0,
            "route" to valueMapOf("tcp_zerocopy" to valueMapOf("compact_waiter" to 1)),
            "fallback" to valueMapOf(
                "to" to "select_stack",
                "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to 1)),
            ),
            "nullable" to null,
            "list" to valueListOf(1, 2),
        )

        val parsed = HoconSupport.parseValue(HoconSupport.render(map)).asValueMap()!!

        assertEquals("6.1.115-test", parsed["release"])
        assertEquals("tcp_zerocopy", parsed["route"].asValueMap()!!.keys.first())
        val fallback = parsed["fallback"].asValueMap()!!
        assertEquals("select_stack", fallback["to"])
        assertEquals(
            1L,
            fallback["route"].asValueMap()!!["select_stack"].asValueMap()!!["waiter_shift"].asLong(),
        )
        assertEquals(2, parsed["list"].asValueList()!!.size)
    }
}
