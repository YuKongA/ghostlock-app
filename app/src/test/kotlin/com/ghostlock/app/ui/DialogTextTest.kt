package com.ghostlock.app.ui

import com.ghostlock.app.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

/**
 * P0 regression: a real device crashed with
 * `Resources$NotFoundException: String resource ID #0x0` at GhostlockUI.kt:582 —
 * the plugin parameter dialog set its message id to 0 and the page rendered it.
 *
 * Two defences are pinned here: a raw 0 cannot be built at all, and the factory
 * turns 0/null into "no line" or the plain text.
 */
class DialogTextTest {

    @Test
    fun `id 0 is not a resource and becomes no line or the plain text`() {
        assertNull(DialogText.of(null, ""))
        assertNull(DialogText.of(0, ""))
        assertEquals(DialogText.Plain("threshold"), DialogText.of(0, "threshold"))
        assertEquals(DialogText.Plain("threshold"), DialogText.of(null, "threshold"))
        assertEquals(
            DialogText.Res(R.string.plugin_param_edit),
            DialogText.of(R.string.plugin_param_edit),
        )
    }

    @Test
    fun `a raw 0 cannot be built into any rendered line`() {
        assertThrows(IllegalArgumentException::class.java) { DialogText.Res(0) }
        assertThrows(IllegalArgumentException::class.java) { PluginLine(0) }
        assertThrows(IllegalArgumentException::class.java) {
            PluginIssueRow(PluginIssueLevel.Error, 0)
        }
        assertThrows(IllegalArgumentException::class.java) {
            PluginHeaderValue.Words(0)
        }
        assertThrows(IllegalArgumentException::class.java) {
            PayloadMessage(PluginIssueLevel.Info, 0)
        }
    }

    /** The exact device path: the plugin parameter dialog. */
    @Test
    fun `the plugin parameter dialog never renders id 0`() {
        val state = pluginParamEditDialogState(GhostlockUiState(), "demo.plugin", "threshold", "7")
        assertEquals(DialogType.INPUT, state.dialogType)
        assertEquals(R.string.plugin_param_edit, state.dialogTitleRes)
        assertNull(state.dialogMessageRes)
        assertEquals("threshold", state.dialogMessage)
        /* Both lines are renderable without a Context and without id 0. */
        assertEquals(
            DialogText.Plain("threshold"),
            DialogText.of(state.dialogMessageRes, state.dialogMessage),
        )
        assertEquals(DialogText.Res(R.string.plugin_param_edit), DialogText.of(state.dialogTitleRes))
    }
}
