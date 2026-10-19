package com.ghostlock.app.log

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeLogTailerTest {
    @Test
    fun parsesNativeBracketLines() {
        val warn = NativeLogTailer.parse("[- 332ms] landing: bring_up refused at open_doors")
        assertEquals(LogLevel.Warn, warn?.first)
        assertEquals("landing: bring_up refused at open_doors", warn?.second)

        val error = NativeLogTailer.parse("[! 16:31:02.719] verify failed: block_at=0 offset=1041960")
        assertEquals(LogLevel.Error, error?.first)
        assertEquals("verify failed: block_at=0 offset=1041960", error?.second)
    }

    @Test
    fun ignoresKotlinLines() {
        assertNull(NativeLogTailer.parse("<*> typed: Assembled43284.trigger -> code=0"))
    }
}
