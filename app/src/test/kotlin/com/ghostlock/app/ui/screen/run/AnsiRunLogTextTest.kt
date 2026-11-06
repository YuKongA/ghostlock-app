package com.ghostlock.app.ui.screen.run

import androidx.compose.ui.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Locks the run-page foreground/reset SGR renderer without a Compose host. */
class AnsiRunLogTextTest {
    /** Approved green spans override Info white and malformed style commands remain inert text. */
    @Test
    fun approvedForegroundOverridesDefaultWithoutRenderingControlBytes() {
        val rendered = ansiRunLogText(
            "plain \u001B[32mPASS\u001B[0m done",
            Color.White,
        )

        assertEquals("plain PASS done", rendered.text)
        assertTrue(rendered.spanStyles.any { range ->
            range.start == 6 && range.end == 10 && range.item.color == Color(0xFF4CAF50)
        })
        assertEquals(
            "bad:[1mstyle",
            ansiRunLogText("bad:\u001B[1mstyle", Color.White).text,
        )
    }
}
