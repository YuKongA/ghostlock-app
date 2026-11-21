package com.ghostlock.app.data.run

import android.content.Context
import android.content.Intent
import android.net.Uri
import com.ghostlock.app.R
import com.ghostlock.app.core.log.LogHub
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogFilePrefix
import com.ghostlock.app.domain.run.LogFileSink
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.LogSinkBinding
import com.ghostlock.app.domain.run.LogSinkEvent
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.LogSinkRole
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import java.io.File
import java.nio.file.Files
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** Android share, chooser failure, font confinement, and localized abort-text tests. */
@RunWith(RobolectricTestRunner::class)
class AndroidRunPresentationAdapterTest {
    /** No accepted run produces a canonical greeting document and launches one chooser. */
    @Test
    fun noPrefixShareCreatesCanonicalDocument() = runBlocking {
        val context = RuntimeEnvironment.getApplication()
        val formatter = LogDocumentFormatter()
        File(context.cacheDir, "logs").deleteRecursively()
        var launched: Intent? = null
        val adapter = adapter(context, LogHub(formatter)) { intent -> launched = intent }

        assertTrue(adapter.shareTerminal("Share terminal"))

        val created = shareFiles(context).single()
        val text = created.readText()
        assertTrue(text.startsWith("GLLOG2\n"))
        assertTrue(text.contains("ghostlock terminal"))
        assertEquals(Intent.ACTION_CHOOSER, requireNotNull(launched).action)
    }

    /** A completed hub copies only the exact flushed prefix returned by its private-file sink. */
    @Test
    fun prefixShareCopiesOnlyFrozenBytes() = runBlocking {
        val context = RuntimeEnvironment.getApplication()
        val formatter = LogDocumentFormatter()
        File(context.cacheDir, "logs").deleteRecursively()
        val hub = LogHub(formatter)
        val source = Files.createTempFile("terminal-prefix", ".log").toFile()
        source.writeText("first\nsecond\n")
        val sink = PrefixFileSink(LogFilePrefix(source.absolutePath, "first\n".toByteArray().size.toLong()))
        assertTrue(
            hub.start(
                RunSessionState(backendKey = "diagnose", backendLabel = "Diagnose", startAllowed = true),
                request(),
                listOf(LogSinkBinding(LogSinkRole.PRIVATE_FILE, sink)),
            ) { RunExecutionResult.Completed(report()) },
        )
        waitFor { hub.state.value.phase == RunSessionPhase.Done }
        val adapter = adapter(context, hub) { }

        assertTrue(adapter.shareTerminal("Share prefix"))

        val created = shareFiles(context).single()
        assertEquals("first\n", created.readText())
    }

    /** Chooser launch exceptions are absorbed into the existing false result. */
    @Test
    fun chooserFailureReturnsFalse() = runBlocking {
        val context = RuntimeEnvironment.getApplication()
        val formatter = LogDocumentFormatter()
        val adapter = adapter(context, LogHub(formatter)) { error("chooser") }

        assertFalse(adapter.shareTerminal("Share"))
    }

    /** Only relative existing files under filesDir resolve, and abort text comes from Android resources. */
    @Test
    fun fontResolutionIsConfinedAndAbortMessageIsLocalized() {
        val context = RuntimeEnvironment.getApplication()
        val formatter = LogDocumentFormatter()
        val adapter = AndroidRunPresentationAdapter(context, LogHub(formatter), formatter)
        val font = File(context.filesDir, "fonts/test.ttf")
        assertTrue(font.parentFile?.mkdirs() == true || font.parentFile?.isDirectory == true)
        font.writeText("font")

        assertEquals(font.canonicalPath, adapter.resolveTerminalFont("fonts/test.ttf"))
        assertNull(adapter.resolveTerminalFont(font.absolutePath))
        assertNull(adapter.resolveTerminalFont("../outside.ttf"))
        assertNull(adapter.resolveTerminalFont("fonts/missing.ttf"))
        assertEquals(context.getString(R.string.run_abort_disabled), adapter.abortDisabledMessage())
    }

    /** Returns current share cache files without mutating previous evidence. */
    private fun shareFiles(context: Context): List<File> =
        File(context.cacheDir, "logs").listFiles().orEmpty().toList()

    /** Creates the adapter with deterministic URI and launcher seams while retaining its real file work. */
    private fun adapter(
        context: Context,
        hub: LogHub,
        launch: (Intent) -> Unit,
    ): AndroidRunPresentationAdapter = AndroidRunPresentationAdapter(
        context = context,
        logHub = hub,
        formatter = LogDocumentFormatter(),
        uriProvider = { _, file -> Uri.parse("content://ghostlock.test/" + file.name) },
        intentLauncher = { _, intent -> launch(intent) },
    )

    /** Waits for one asynchronous hub completion. */
    private suspend fun waitFor(condition: () -> Boolean) = withTimeout(5_000L) {
        while (!condition()) delay(5L)
    }

    /** Returns the stable sink request used by the prefix case. */
    private fun request(): LogSinkRequest = LogSinkRequest(
        enabled = false,
        runId = "0123456789abcdef0123456789abcdef",
        generation = 1L,
        backendKey = "diagnose",
        relativePath = "Download/ghostlock-log",
        formatConfig = LogFormatConfig(false),
    )

    /** Returns one legal diagnostic report. */
    private fun report(): RunReport = RunReport(
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
        diagnosticOutcome = DiagnosticOutcome.PASS,
        backendPrimitive = StageResult.SUCCESS,
        rootScriptDispatch = StageResult.SKIPPED,
        payloadItems = emptyList(),
        payloadOverall = StageResult.SKIPPED,
        finishItems = emptyList(),
        finishOverall = StageResult.SKIPPED,
        backendCleanup = StageResult.SUCCESS,
        terminal = RunTerminal.DIAGNOSTIC_COMPLETE,
    )

    /** Private-file sink that publishes one predetermined completed prefix on End. */
    private class PrefixFileSink(private val prefix: LogFilePrefix) : LogFileSink {
        private val completion = CompletableDeferred<LogFilePrefix>()
        override val finished: Deferred<LogFilePrefix> = completion

        override fun tryEmit(event: LogSinkEvent): Boolean {
            if (event is LogSinkEvent.End) completion.complete(prefix)
            return true
        }

        override fun abort(reason: String) {
            completion.completeExceptionally(IllegalStateException(reason))
        }
    }
}
