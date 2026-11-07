package com.ghostlock.app.data.run

import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.provider.MediaStore
import com.ghostlock.app.domain.run.LogFilePrefix
import com.ghostlock.app.domain.run.LogFileSink
import com.ghostlock.app.domain.run.LogSink
import com.ghostlock.app.domain.run.LogSinkBinding
import com.ghostlock.app.domain.run.LogSinkEvent
import com.ghostlock.app.domain.run.LogSinkFailure
import com.ghostlock.app.domain.run.LogSinkFailureKind
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.LogSinkRole
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch

/** Testable MediaStore operations retained inside the Android data layer. */
internal interface LogMediaStoreGateway {
    fun insert(values: ContentValues): Uri?
    fun open(uri: Uri): OutputStream?
    fun delete(uri: Uri): Int
}

/** Production ContentResolver implementation of [LogMediaStoreGateway]. */
private class AndroidLogMediaStoreGateway(private val resolver: ContentResolver) : LogMediaStoreGateway {
    override fun insert(values: ContentValues): Uri? =
        resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
    override fun open(uri: Uri): OutputStream? = resolver.openOutputStream(uri, "w")
    override fun delete(uri: Uri): Int = resolver.delete(uri, null, null)
}

/**
 * Creates run-scoped private-file and optional MediaStore sink instances for the adapter.
 * Workers do not start or perform platform I/O until LogHub accepts the run and emits Start.
 */
class AndroidLogSinkFactory internal constructor(
    private val privateDirectory: File,
    private val gateway: LogMediaStoreGateway,
    private val scope: CoroutineScope,
) {
    constructor(context: Context) : this(
        privateDirectory = File(context.applicationContext.filesDir, "run-logs"),
        gateway = AndroidLogMediaStoreGateway(context.applicationContext.contentResolver),
        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    )

    /** @return Ready but unopened per-run storage sinks; the private-file role is always present. */
    fun create(request: LogSinkRequest, onFailure: (LogSinkFailure) -> Unit): List<LogSinkBinding> {
        val file = runCatching { PrivateFileSink(privateDirectory, request, scope, onFailure) }
            .getOrElse { problem -> FailedFileSink(problem, onFailure) }
        val media = if (request.enabled) {
            runCatching { MediaStoreSink(request, gateway, scope, onFailure) }.getOrElse { problem ->
                onFailure(LogSinkFailure(LogSinkFailureKind.OPEN, "automatic export setup: ${problem::class.java.simpleName}"))
                null
            }
        } else null
        return buildList {
            add(LogSinkBinding(LogSinkRole.PRIVATE_FILE, file))
            media?.let { add(LogSinkBinding(LogSinkRole.MEDIA_STORE, it)) }
        }
    }

    /** Common nonblocking ingress and first-failure handling; workers never share an I/O queue. */
    private abstract class WorkerSink(
        protected val request: LogSinkRequest,
        scope: CoroutineScope,
        private val onFailure: (LogSinkFailure) -> Unit,
    ) : LogSink {
        protected val events = Channel<LogSinkEvent>(Channel.UNLIMITED)
        private val failure = AtomicReference<LogSinkFailure?>(null)
        private val abortReason = AtomicReference<String?>(null)

        /** A rejected prebuilt sink never starts its worker or performs platform I/O. */
        private val worker = scope.launch(start = CoroutineStart.LAZY) { runWorker() }

        override fun tryEmit(event: LogSinkEvent): Boolean {
            if (abortReason.get() != null || failure.get() != null) {
                rejectBarrier(event, "log sink is closed")
                return false
            }
            val sent = events.trySend(event).isSuccess
            if (!sent) rejectBarrier(event, "log sink rejected event")
            if (sent && event is LogSinkEvent.Start) worker.start()
            return sent
        }

        override open fun abort(reason: String) {
            abortReason.compareAndSet(null, reason)
            events.close()
            worker.cancel(CancellationException(reason))
            failPending(reason)
        }

        protected fun checkAbort() {
            val reason = abortReason.get()
            if (reason != null) error(reason)
        }

        protected fun recordFailure(kind: LogSinkFailureKind, detail: String) {
            val first = LogSinkFailure(kind, detail)
            if (failure.compareAndSet(null, first)) runCatching { onFailure(first) }
            events.close()
        }

        protected fun rejectBarrier(event: LogSinkEvent, reason: String) {
            if (event is LogSinkEvent.FileBarrier) {
                event.completion.completeExceptionally(IllegalStateException(reason))
            }
        }

        protected fun failPending(reason: String) {
            while (true) {
                val event = events.tryReceive().getOrNull() ?: break
                rejectBarrier(event, reason)
            }
        }

        protected abstract suspend fun runWorker()
    }

    /** Private-file owner; barriers flush the exact prefix and terminal completion follows close. */
    private class PrivateFileSink(
        directory: File,
        request: LogSinkRequest,
        scope: CoroutineScope,
        onFailure: (LogSinkFailure) -> Unit,
    ) : WorkerSink(request, scope, onFailure), LogFileSink {
        private val target = File(directory, "g${request.generation}-${safeName(request.runId)}.log.txt")
        private val completed = CompletableDeferred<LogFilePrefix>()
        override val finished = completed

        override fun abort(reason: String) {
            super.abort(reason)
            completed.completeExceptionally(CancellationException(reason))
        }

        override suspend fun runWorker() {
            var output: BufferedOutputStream? = null
            var bytesWritten = 0L
            var ended = false
            var failureKind = LogSinkFailureKind.OPEN
            try {
                checkAbort()
                check(target.parentFile?.mkdirs() == true || target.parentFile?.isDirectory == true) {
                    "cannot create private log directory"
                }
                val writer = BufferedOutputStream(FileOutputStream(target), BUFFER_BYTES)
                output = writer
                failureKind = LogSinkFailureKind.WRITE
                for (event in events) {
                    checkAbort()
                    when (event) {
                        is LogSinkEvent.Start -> event.rows.forEach { row ->
                            writer.write(row); bytesWritten = Math.addExact(bytesWritten, row.size.toLong())
                        }
                        is LogSinkEvent.Entry -> {
                            writer.write(event.row)
                            bytesWritten = Math.addExact(bytesWritten, event.row.size.toLong())
                        }
                        is LogSinkEvent.FileBarrier -> {
                            failureKind = LogSinkFailureKind.FLUSH
                            try {
                                writer.flush()
                                event.completion.complete(LogFilePrefix(target.absolutePath, bytesWritten))
                            } catch (problem: Throwable) {
                                event.completion.completeExceptionally(problem)
                                throw problem
                            }
                            failureKind = LogSinkFailureKind.WRITE
                        }
                        is LogSinkEvent.End -> {
                            event.rows.forEach { row ->
                                writer.write(row); bytesWritten = Math.addExact(bytesWritten, row.size.toLong())
                            }
                            failureKind = LogSinkFailureKind.FLUSH
                            writer.flush()
                            failureKind = LogSinkFailureKind.CLOSE
                            writer.close()
                            output = null
                            ended = true
                            completed.complete(LogFilePrefix(target.absolutePath, bytesWritten))
                            break
                        }
                        LogSinkEvent.UiClear -> Unit
                    }
                }
                if (!ended) error("private log stream ended before terminal")
            } catch (problem: Throwable) {
                recordFailure(failureKind, "private log: ${problem::class.java.simpleName}")
                completed.completeExceptionally(problem)
                failPending("private log failed")
            } finally {
                runCatching { output?.close() }.onFailure {
                    recordFailure(LogSinkFailureKind.CLOSE, "private log close failed")
                    completed.completeExceptionally(it)
                }
                events.close()
                failPending("private log closed")
            }
        }
    }

    /** Keeps an accepted Flow running when only the private-file sink could not be constructed. */
    private class FailedFileSink(
        private val problem: Throwable,
        private val onFailure: (LogSinkFailure) -> Unit,
    ) : LogFileSink {
        private val reported = AtomicBoolean(false)
        override val finished = CompletableDeferred<LogFilePrefix>().also { it.completeExceptionally(problem) }

        override fun tryEmit(event: LogSinkEvent): Boolean {
            if (event is LogSinkEvent.Start && reported.compareAndSet(false, true)) {
                runCatching { onFailure(LogSinkFailure(LogSinkFailureKind.OPEN,
                    "private log setup: ${problem::class.java.simpleName}")) }
            }
            if (event is LogSinkEvent.FileBarrier) event.completion.completeExceptionally(problem)
            return false
        }

        override fun abort(reason: String) = Unit
    }

    /** MediaStore owns its own blocked-provider backlog and never delays the private-file worker. */
    private class MediaStoreSink(
        request: LogSinkRequest,
        private val gateway: LogMediaStoreGateway,
        scope: CoroutineScope,
        onFailure: (LogSinkFailure) -> Unit,
    ) : WorkerSink(request, scope, onFailure) {
        override suspend fun runWorker() {
            var output: BufferedOutputStream? = null
            var failureKind = LogSinkFailureKind.OPEN
            try {
                checkAbort()
                check(isSafeDownloadPath(request.relativePath)) { "invalid Downloads-relative path" }
                val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
                val values = ContentValues().apply {
                    put(MediaStore.MediaColumns.DISPLAY_NAME, "ghostlock-run-${request.runId}-g${request.generation}.log.txt")
                    put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
                    put(MediaStore.MediaColumns.RELATIVE_PATH, "${request.relativePath}/$stamp")
                }
                val uri = gateway.insert(values) ?: error("MediaStore insert failed")
                val raw = runCatching { gateway.open(uri) }.getOrNull()
                if (raw == null) {
                    runCatching { gateway.delete(uri) }
                    error("MediaStore open failed")
                }
                val writer = BufferedOutputStream(raw, BUFFER_BYTES)
                output = writer
                failureKind = LogSinkFailureKind.WRITE
                var ended = false
                for (event in events) {
                    checkAbort()
                    when (event) {
                        is LogSinkEvent.Start -> event.rows.forEach(writer::write)
                        is LogSinkEvent.Entry -> writer.write(event.row)
                        is LogSinkEvent.End -> {
                            event.rows.forEach(writer::write)
                            failureKind = LogSinkFailureKind.FLUSH
                            writer.flush()
                            failureKind = LogSinkFailureKind.CLOSE
                            writer.close()
                            output = null
                            ended = true
                            break
                        }
                        is LogSinkEvent.FileBarrier, LogSinkEvent.UiClear -> Unit
                    }
                }
                if (!ended) error("MediaStore stream ended before terminal")
            } catch (problem: Throwable) {
                recordFailure(failureKind, "automatic export: ${problem::class.java.simpleName}")
                failPending("MediaStore log failed")
            } finally {
                runCatching { output?.close() }.onFailure {
                    recordFailure(LogSinkFailureKind.CLOSE, "automatic export close failed")
                }
                events.close()
                failPending("MediaStore log closed")
            }
        }
    }

    private companion object {
        const val BUFFER_BYTES = 64 * 1024
        fun safeName(value: String): String = value.map { ch ->
            if (ch.isLetterOrDigit() || ch == '-' || ch == '_') ch else '_'
        }.joinToString("")
        fun isSafeDownloadPath(value: String): Boolean =
            (value == "Download" || value.startsWith("Download/")) &&
                value.split('/').all { it.isNotEmpty() && it != "." && it != ".." }
    }
}

/** Copies exactly one flushed prefix while a live producer may append beyond it. */
fun copyLogPrefix(prefix: LogFilePrefix, target: File) {
    File(prefix.path).inputStream().buffered().use { input ->
        target.outputStream().buffered().use { output ->
            var remaining = prefix.byteLength
            val buffer = ByteArray(64 * 1024)
            while (remaining > 0L) {
                val count = input.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                check(count > 0) { "private log ended before flushed prefix" }
                output.write(buffer, 0, count)
                remaining -= count
            }
        }
    }
}
