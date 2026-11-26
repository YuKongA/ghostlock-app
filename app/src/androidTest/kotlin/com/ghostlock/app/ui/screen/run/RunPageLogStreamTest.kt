package com.ghostlock.app.ui.screen.run

import androidx.activity.ComponentActivity
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasScrollToIndexAction
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollToIndex
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.LogRecord
import com.ghostlock.app.domain.run.LogRowsSnapshot
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.theme.GhostLockAppTheme
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Both renderers follow appended snapshots and retain earlier rows in committed order. */
@RunWith(AndroidJUnit4::class)
class RunPageLogStreamTest {
    @get:Rule val composeRule = createAndroidComposeRule<ComponentActivity>()

    @Test
    fun materialRendersTailInfoFromFullHistory() {
        assertAppendedTailAndHistory(UiMode.Material)
    }

    @Test
    fun miuixRendersTailInfoFromFullHistory() {
        assertAppendedTailAndHistory(UiMode.Miuix)
    }

    private fun assertAppendedTailAndHistory(mode: UiMode) {
        val current = mutableStateOf(state(includeSummary = false))
        composeRule.setContent {
            GhostLockAppTheme(uiMode = mode) {
                if (mode == UiMode.Material) {
                    RunPageMaterial(current.value, actions(), 0.dp)
                } else {
                    RunPageMiuix(current.value, actions(), 0.dp)
                }
            }
        }
        composeRule.waitForIdle()
        composeRule.onNodeWithText("debug-5000").assertIsDisplayed()
        val rows = composeRule.onNode(hasScrollToIndexAction())
        rows.performScrollToIndex(0)
        assertVisibleOrder("debug-1", "debug-2")

        composeRule.runOnIdle { current.value = state(includeSummary = true) }
        composeRule.waitForIdle()
        // Do not scroll to the appended row: this assertion exercises the renderer's own tail follow.
        composeRule.onNodeWithText("Product success: YES").assertIsDisplayed()

        rows.performScrollToIndex(0)
        assertVisibleOrder("debug-1", "debug-2")
        rows.performScrollToIndex(4_998)
        assertVisibleOrder("debug-4999", "debug-5000", "Product success: YES")
    }

    private fun assertVisibleOrder(vararg messages: String) {
        val bounds = messages.map { message ->
            composeRule.onNodeWithText(message).assertIsDisplayed().fetchSemanticsNode().boundsInRoot
        }
        bounds.zipWithNext().forEach { (previous, next) ->
            assertTrue("Rows must remain in committed order", previous.bottom <= next.top)
        }
    }

    /** Builds enough earlier rows to require a virtualized tail jump without hiding the final Info row. */
    private fun state(includeSummary: Boolean): RunUiState {
        val prefix = (1L..5_000L).map { id ->
            LogRecord(
                LogOrigin.Native,
                LogLevel.Debug,
                "debug-$id",
                commitId = id,
                sourceElapsedMs = 5_001L - id,
            )
        }
        val rows = if (includeSummary) prefix + LogRecord(
            LogOrigin.Kotlin,
            LogLevel.Info,
            "Product success: \u001B[32mYES\u001B[0m",
            commitId = 5_001L,
            sourceElapsedMs = 0L,
        ) else prefix
        return RunUiState(
            backendKey = "DIAGNOSE", backendLabel = "Diagnose", startAllowed = false,
            phase = if (includeSummary) RunUiPhase.Done else RunUiPhase.Running,
            entries = LogRowsSnapshot.fromRecords(rows, token = 1L, revision = if (includeSummary) 2L else 1L),
        )
    }

    /** Inert callbacks keep the UI gate read-only. */
    private fun actions() = RunActions({}, {}, {}, {}, {})
}
