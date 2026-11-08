package com.ghostlock.app.ui.screen.run

import androidx.activity.ComponentActivity
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.LogRecord
import com.ghostlock.app.domain.run.LogRowsSnapshot
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.theme.GhostLockAppTheme
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/** Both renderers preserve committed order despite source-clock reversal and show summaries as ordinary Info rows. */
@RunWith(AndroidJUnit4::class)
class RunPageLogStreamTest {
    @get:Rule val composeRule = createAndroidComposeRule<ComponentActivity>()

    @Test
    fun materialRendersTailInfoFromFullHistory() {
        composeRule.setContent {
            GhostLockAppTheme(uiMode = UiMode.Material) {
                RunPageMaterial(state(), actions(), 0.dp)
            }
        }
        composeRule.waitForIdle()
        composeRule.onNodeWithText("Product success: YES", substring = true).assertExists()
    }

    @Test
    fun miuixRendersTailInfoFromFullHistory() {
        composeRule.setContent {
            GhostLockAppTheme(uiMode = UiMode.Miuix) {
                RunPageMiuix(state(), actions(), 0.dp)
            }
        }
        composeRule.waitForIdle()
        composeRule.onNodeWithText("Product success: YES", substring = true).assertExists()
    }

    /** Builds enough earlier rows to require a virtualized tail jump without hiding the final Info row. */
    private fun state(): RunUiState {
        val rows = (1L..5_000L).map { id ->
            LogRecord(
                LogOrigin.Native,
                LogLevel.Debug,
                "debug-$id",
                commitId = id,
                sourceElapsedMs = 5_001L - id,
            )
        } + LogRecord(
            LogOrigin.Kotlin,
            LogLevel.Info,
            "Product success: \u001B[32mYES\u001B[0m",
            commitId = 5_001L,
            sourceElapsedMs = 0L,
        )
        return RunUiState(
            backendKey = "DIAGNOSE", backendLabel = "Diagnose", startAllowed = false,
            phase = RunUiPhase.Done, entries = LogRowsSnapshot.fromRecords(rows, token = 1L, revision = 1L),
        )
    }

    /** Inert callbacks keep the UI gate read-only. */
    private fun actions() = RunActions({}, {}, {}, {}, {})
}
