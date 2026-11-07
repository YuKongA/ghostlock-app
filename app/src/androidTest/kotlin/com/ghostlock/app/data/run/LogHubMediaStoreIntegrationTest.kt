package com.ghostlock.app.data.run

import android.content.ContentResolver
import android.content.ContentUris
import android.net.Uri
import android.provider.MediaStore
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.run.LogHub
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.LogRecord
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import java.util.UUID
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** AVD integration gate for the production log-hub-to-MediaStore automatic-export path. */
@RunWith(AndroidJUnit4::class)
class LogHubMediaStoreIntegrationTest {
    /** A >10 MiB accepted run reaches private file and MediaStore with byte-identical GLLOG2. */
    @Test
    fun acceptedRunMatchesManualDocumentAndDeletesItsOwnRow() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val resolver = context.contentResolver
        val runId = UUID.randomUUID().toString().replace("-", "")
        val generation = 1L
        val displayName = "ghostlock-run-$runId-g$generation.log.txt"
        val logHub = LogHub(LogDocumentFormatter())
        val sinkRequest = LogSinkRequest(
            enabled = true,
            runId = runId,
            generation = generation,
            backendKey = BACKEND_KEY,
            relativePath = EXPORT_PATH,
            formatConfig = LogFormatConfig(showTimestamp = false),
        )
        val storageSinks = AndroidLogSinkFactory(context).create(sinkRequest) { failure ->
            logHub.reportSinkFailure(sinkRequest, failure)
        }
        var stored: StoredDocument? = null

        try {
            assertTrue(
                logHub.start(
                    context = RunSessionState(
                        backendKey = BACKEND_KEY,
                        backendLabel = "Debug Probe",
                        startAllowed = true,
                        abortable = true,
                    ),
                    sinkRequest = sinkRequest,
                    storageSinks = storageSinks,
                ) { emit ->
                    val message = "x".repeat(640)
                    repeat(16_384) { emit(LogRecord(LogOrigin.Native, LogLevel.Info, message)) }
                    RunExecutionResult.Completed(diagnosticReport())
                },
            )
            withTimeout(WAIT_TIMEOUT_MS) {
                logHub.state.first { state -> state.phase == RunSessionPhase.Done }
                while (logHub.isRunning()) yield()
            }

            val prefix = withTimeout(WAIT_TIMEOUT_MS) { requireNotNull(logHub.filePrefix()) }
            val manualBytes = java.io.File(prefix.path).readBytes()
            assertEquals(prefix.byteLength, manualBytes.size.toLong())
            assertTrue(manualBytes.size > 10 * 1024 * 1024)
            withTimeout(WAIT_TIMEOUT_MS) {
                while (stored == null) {
                    val candidate = readDocument(resolver, displayName)
                    if (candidate?.bytes?.contentEquals(manualBytes) == true) {
                        stored = candidate
                    } else {
                        delay(POLL_INTERVAL_MS)
                    }
                }
            }

            val completed = requireNotNull(stored)
            assertArrayEquals(manualBytes, completed.bytes)
            assertTrue(completed.bytes.toString(Charsets.UTF_8).endsWith("end\tterminal_received=true\n"))
            assertFalse(logHub.state.value.report?.isProductSuccess == true)
            assertEquals(1, resolver.delete(completed.uri, null, null))
            stored = null
        } finally {
            stored?.let { document -> resolver.delete(document.uri, null, null) }
            deleteRowsByDisplayName(resolver, displayName)
        }
    }

    /** @return One complete diagnostic-only typed terminal report. */
    private fun diagnosticReport(): RunReport = RunReport(
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

    /** Reads the newest row with [displayName], including its currently visible bytes. */
    private fun readDocument(
        resolver: ContentResolver,
        displayName: String,
    ): StoredDocument? = resolver.query(
        MediaStore.Downloads.EXTERNAL_CONTENT_URI,
        arrayOf(MediaStore.MediaColumns._ID),
        "${MediaStore.MediaColumns.DISPLAY_NAME} = ?",
        arrayOf(displayName),
        "${MediaStore.MediaColumns._ID} DESC",
    )?.use { cursor ->
        if (!cursor.moveToFirst()) return@use null
        val id = cursor.getLong(cursor.getColumnIndexOrThrow(MediaStore.MediaColumns._ID))
        val uri = ContentUris.withAppendedId(MediaStore.Downloads.EXTERNAL_CONTENT_URI, id)
        val bytes = runCatching {
            resolver.openInputStream(uri)?.use { input -> input.readBytes() }
        }.getOrNull() ?: return@use null
        StoredDocument(uri, bytes)
    }

    /** Deletes only rows bearing the unique display name allocated by this test transaction. */
    private fun deleteRowsByDisplayName(
        resolver: ContentResolver,
        displayName: String,
    ) {
        resolver.delete(
            MediaStore.Downloads.EXTERNAL_CONTENT_URI,
            "${MediaStore.MediaColumns.DISPLAY_NAME} = ?",
            arrayOf(displayName),
        )
    }

    /** Immutable observation of one MediaStore row. */
    private data class StoredDocument(
        val uri: Uri,
        val bytes: ByteArray,
    )

    private companion object {
        const val BACKEND_KEY: String = "DIAGNOSE"
        const val EXPORT_PATH: String = "Download/ghostlock-log"
        const val WAIT_TIMEOUT_MS: Long = 60_000L
        const val POLL_INTERVAL_MS: Long = 50L
    }
}
