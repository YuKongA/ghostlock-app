package com.ghostlock.app.data.run

import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.provider.MediaStore
import com.ghostlock.app.domain.run.RunLogSink
import com.ghostlock.app.domain.run.RunLogSinkEmitResult
import com.ghostlock.app.domain.run.RunLogSinkFactory
import com.ghostlock.app.domain.run.RunLogSinkFailure
import com.ghostlock.app.domain.run.RunLogSinkFailureKind
import com.ghostlock.app.domain.run.RunLogSinkRequest
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

/** Testable MediaStore operations retained inside the Android data layer. */
internal interface RunLogMediaStoreGateway {
    /** @return Inserted Downloads URI, or null when insertion failed. */
    fun insert(values: ContentValues): Uri?

    /** @return Writable output stream for [uri], or null when opening failed. */
    fun open(uri: Uri): OutputStream?

    /** @return Provider delete count; failure is represented as zero. */
    fun delete(uri: Uri): Int
}

/** Production ContentResolver implementation of [RunLogMediaStoreGateway]. */
private class AndroidRunLogMediaStoreGateway(
    private val resolver: ContentResolver,
) : RunLogMediaStoreGateway {
    override fun insert(values: ContentValues): Uri? =
        resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)

    override fun open(uri: Uri): OutputStream? = resolver.openOutputStream(uri, "w")

    override fun delete(uri: Uri): Int = resolver.delete(uri, null, null)
}

/**
 * Immediate factory for one-channel, one-worker Android run-log sinks.
 *
 * @param gateway Android MediaStore boundary used only by the worker.
 * @param scope Application-lifetime IO supervisor.
 * @note [create] allocates and schedules only. The worker exclusively owns URI and stream operations.
 */
class AndroidRunLogSinkFactory internal constructor(
    private val gateway: RunLogMediaStoreGateway,
    private val scope: CoroutineScope,
) : RunLogSinkFactory {
    /** Creates the production factory over the application ContentResolver and one IO supervisor. */
    constructor(context: Context) : this(
        gateway = AndroidRunLogMediaStoreGateway(context.applicationContext.contentResolver),
        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    )

    /** Allocates one sink and schedules its platform work without waiting for gateway I/O. */
    override fun create(
        request: RunLogSinkRequest,
        onFailure: (RunLogSinkFailure) -> Unit,
    ): RunLogSink = AndroidRunLogSink(
        request = request,
        gateway = gateway,
        scope = scope,
        onFailure = onFailure,
    )

    /** One bounded producer queue and its sole platform-resource owner. */
    private class AndroidRunLogSink(
        private val request: RunLogSinkRequest,
        private val gateway: RunLogMediaStoreGateway,
        scope: CoroutineScope,
        private val onFailure: (RunLogSinkFailure) -> Unit,
    ) : RunLogSink {
        private val commands = Channel<List<ByteArray>>(COMMAND_CAPACITY)
        private val accepting = AtomicBoolean(true)
        private val closeAttempted = AtomicBoolean(false)
        private val firstFailure = AtomicReference<RunLogSinkFailure?>(null)

        init {
            scope.launch {
                val opened = openDocument()
                if (opened is OpenDocumentResult.Failed) {
                    fail(opened.failure)
                    stopAccepting()
                    discardQueuedCommands()
                    return@launch
                }
                val output = (opened as OpenDocumentResult.Opened).output
                try {
                    for (rows in commands) {
                        rows.forEach { row -> writeRow(output, row) }
                    }
                } catch (_: SinkStoppedException) {
                    discardQueuedCommands()
                } catch (_: Throwable) {
                    fail(
                        RunLogSinkFailure(
                            RunLogSinkFailureKind.WRITE,
                            "automatic export worker failed",
                        ),
                    )
                    discardQueuedCommands()
                } finally {
                    stopAccepting()
                    closeStream(output)
                }
            }
        }

        /** Copies one complete command into the sole bounded queue without platform I/O. */
        override fun tryEmit(rows: List<ByteArray>): RunLogSinkEmitResult {
            if (rows.isEmpty() || rows.any { row -> row.isEmpty() || row.last() != LF_BYTE }) {
                fail(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.WRITE,
                        "automatic export received a non-row command",
                    ),
                )
                return RunLogSinkEmitResult.REJECTED
            }
            if (!accepting.get()) return RunLogSinkEmitResult.REJECTED
            val command = rows.map { row -> row.copyOf() }
            val sent = commands.trySend(command)
            if (sent.isSuccess) return RunLogSinkEmitResult.ACCEPTED
            if (!sent.isClosed) {
                fail(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.QUEUE_FULL,
                        "automatic export command queue is full",
                    ),
                )
            }
            return RunLogSinkEmitResult.REJECTED
        }

        /** Closes command acceptance out of band; the worker drains the already accepted prefix. */
        override fun finish() {
            stopAccepting()
        }

        /** Performs insert/open on the worker and deletes exactly one orphan URI after open failure. */
        private fun openDocument(): OpenDocumentResult {
            val path = request.relativePath.takeIf(::isSafeDownloadPath)
                ?: return OpenDocumentResult.Failed(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.OPEN,
                        "automatic export path is not Downloads-relative",
                    ),
                )
            val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
            val values = ContentValues().apply {
                put(
                    MediaStore.MediaColumns.DISPLAY_NAME,
                    "ghostlock-run-${request.runId}-g${request.generation}.log.txt",
                )
                put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                put(MediaStore.MediaColumns.RELATIVE_PATH, "$path/$stamp")
            }
            val uri = runCatching { gateway.insert(values) }.getOrNull()
                ?: return OpenDocumentResult.Failed(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.OPEN,
                        "MediaStore insert failed",
                    ),
                )
            val output = runCatching { gateway.open(uri) }.getOrNull()
            if (output == null) {
                runCatching { gateway.delete(uri) }
                return OpenDocumentResult.Failed(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.OPEN,
                        "MediaStore output stream open failed",
                    ),
                )
            }
            return OpenDocumentResult.Opened(output)
        }

        /** Writes and flushes one row; a provider may expose a partial row before throwing. */
        private fun writeRow(output: OutputStream, row: ByteArray) {
            try {
                output.write(row)
            } catch (problem: Throwable) {
                fail(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.WRITE,
                        "automatic export write failed",
                    ),
                )
                throw SinkStoppedException(problem)
            }
            try {
                output.flush()
            } catch (problem: Throwable) {
                fail(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.FLUSH,
                        "automatic export flush failed",
                    ),
                )
                throw SinkStoppedException(problem)
            }
        }

        /** Closes the worker-owned stream once; an earlier failure keeps precedence. */
        private fun closeStream(output: OutputStream) {
            if (!closeAttempted.compareAndSet(false, true)) return
            try {
                output.close()
            } catch (_: Throwable) {
                fail(
                    RunLogSinkFailure(
                        RunLogSinkFailureKind.CLOSE,
                        "automatic export close failed",
                    ),
                )
            }
        }

        /** Latches and reports only the first failure, then stops accepting commands. */
        private fun fail(failure: RunLogSinkFailure) {
            if (!firstFailure.compareAndSet(null, failure)) return
            accepting.set(false)
            commands.close(SinkStoppedException(failure))
            runCatching { onFailure(failure) }
        }

        /** Stops producers without manufacturing a failure or occupying a queue slot. */
        private fun stopAccepting() {
            accepting.set(false)
            commands.close()
        }

        /** Releases immutable queued copies after open/write failure makes them unwritable. */
        private fun discardQueuedCommands() {
            while (commands.tryReceive().isSuccess) {}
        }

        /** @return True only for a traversal-free Downloads path accepted by MediaStore. */
        private fun isSafeDownloadPath(value: String): Boolean {
            if (value != "Download" && !value.startsWith("Download/")) return false
            return value.split('/').all { segment ->
                segment.isNotEmpty() && segment != "." && segment != ".."
            }
        }
    }

    /** Worker-only result of the single MediaStore open attempt. */
    private sealed interface OpenDocumentResult {
        data class Opened(val output: OutputStream) : OpenDocumentResult
        data class Failed(val failure: RunLogSinkFailure) : OpenDocumentResult
    }

    /** Internal channel/worker stop cause; it never crosses the neutral contract. */
    private class SinkStoppedException(cause: Any) : RuntimeException(
        when (cause) {
            is RunLogSinkFailure -> cause.detail
            is Throwable -> cause.message
            else -> cause.toString()
        },
        cause as? Throwable,
    )

    private companion object {
        const val COMMAND_CAPACITY: Int = 512
        const val LF_BYTE: Byte = 10
    }
}
