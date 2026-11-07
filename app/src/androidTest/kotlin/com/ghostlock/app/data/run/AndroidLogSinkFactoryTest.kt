package com.ghostlock.app.data.run

import android.content.ContentValues
import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.LogDocumentIdentity
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.LogRecord
import com.ghostlock.app.domain.run.LogSinkEvent
import com.ghostlock.app.domain.run.LogSink
import com.ghostlock.app.domain.run.LogFileSink
import com.ghostlock.app.domain.run.LogSinkFailure
import com.ghostlock.app.domain.run.LogSinkFailureKind
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.LogSinkRole
import com.ghostlock.app.domain.run.RunSessionPhase
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Android sink-worker gates for independent backlogs, exact prefix barriers, and failure isolation. */
@RunWith(AndroidJUnit4::class)
class AndroidLogSinkFactoryTest {
    /** Unaccepted, already-instantiated sinks never touch private storage or MediaStore. */
    @Test
    fun prebuiltSinksStayUnopenedWhenAbortedBeforeStart() = runBlocking {
        val cache = InstrumentationRegistry.getInstrumentation().targetContext.cacheDir
        val directory = java.io.File(cache, "sink-rejected-${System.nanoTime()}")
        val gateway = FakeGateway(RecordingOutputStream())
        val factory = AndroidLogSinkFactory(
            privateDirectory = directory,
            gateway = gateway,
            scope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
        )
        val bindings = factory.create(request()) { error("unexpected sink failure: $it") }
        kotlinx.coroutines.delay(50L)
        assertFalse(directory.exists())
        assertEquals(0, gateway.inserts.get())

        bindings.forEach { it.sink.abort("run rejected") }
        kotlinx.coroutines.delay(50L)
        assertFalse(directory.exists())
        assertEquals(0, gateway.inserts.get())
        assertTrue((bindings.single { it.role == LogSinkRole.PRIVATE_FILE }.sink as LogFileSink).finished.isCompleted)
    }

    @Test
    fun blockedMediaOpenDoesNotDelayPrivateBarrierOrTerminal() = runBlocking {
        val media = RecordingOutputStream()
        val gateway = FakeGateway(media, blockOpen = true)
        val factory = factory(gateway)
        val failures = CopyOnWriteArrayList<LogSinkFailure>()
        val sinks = createSinks(factory, request(), failures)
        val (start, entry, end) = events()
        assertTrue(sinks.file.tryEmit(start))
        assertTrue(sinks.mediaStore!!.tryEmit(start))
        assertTrue(sinks.file.tryEmit(entry))
        assertTrue(sinks.mediaStore.tryEmit(entry))
        val barrier = CompletableDeferred<com.ghostlock.app.domain.run.LogFilePrefix>()
        assertTrue(sinks.file.tryEmit(LogSinkEvent.FileBarrier(barrier)))
        val prefix = withTimeout(5_000L) { barrier.await() }
        assertTrue(prefix.byteLength > 0L)
        assertFalse(java.io.File(prefix.path).readText().contains("terminal_received"))
        assertTrue(sinks.file.tryEmit(end))
        assertTrue(sinks.mediaStore.tryEmit(end))
        val finished = withTimeout(5_000L) { sinks.file.finished.await() }
        assertTrue(java.io.File(finished.path).readText().endsWith("end\tterminal_received=true\n"))
        assertEquals(0, failures.size)
        gateway.releaseOpen.countDown()
        assertTrue(media.closed.await(5L, TimeUnit.SECONDS))
        assertArrayEquals(java.io.File(finished.path).readBytes(), media.bytes())
        assertTrue(media.flushes.get() < 3)
    }

    @Test
    fun mediaOpenFailureLeavesPrivateFileUsable() = runBlocking {
        val gateway = FakeGateway(null)
        val failures = CopyOnWriteArrayList<LogSinkFailure>()
        val sinks = createSinks(factory(gateway), request(), failures)
        val (start, entry, end) = events()
        assertTrue(sinks.file.tryEmit(start))
        assertTrue(sinks.mediaStore!!.tryEmit(start))
        assertTrue(sinks.file.tryEmit(entry))
        assertTrue(sinks.file.tryEmit(end))
        val finished = withTimeout(5_000L) { sinks.file.finished.await() }
        assertTrue(java.io.File(finished.path).length() > 0L)
        withTimeout(5_000L) { while (failures.isEmpty()) kotlinx.coroutines.delay(5L) }
        assertEquals(LogSinkFailureKind.OPEN, failures.first().kind)
        assertEquals(1, failures.size)
        assertEquals(1, gateway.deletes.get())
    }

    @Test
    fun invalidMediaPathCannotDisablePrivateFile() = runBlocking {
        val gateway = FakeGateway(RecordingOutputStream())
        val failures = CopyOnWriteArrayList<LogSinkFailure>()
        val sinks = createSinks(factory(gateway), request().copy(relativePath = "Pictures/other"), failures)
        val (start, entry, end) = events()
        sinks.file.tryEmit(start)
        sinks.mediaStore!!.tryEmit(start)
        sinks.file.tryEmit(entry)
        sinks.file.tryEmit(end)
        val finished = withTimeout(5_000L) { sinks.file.finished.await() }
        assertTrue(java.io.File(finished.path).readText().contains("one-record"))
        withTimeout(5_000L) { while (failures.isEmpty()) kotlinx.coroutines.delay(5L) }
        assertEquals(LogSinkFailureKind.OPEN, failures.first().kind)
        assertEquals(0, gateway.inserts.get())
    }

    private fun events(): Triple<LogSinkEvent.Start, LogSinkEvent.Entry, LogSinkEvent.End> {
        val formatter = LogDocumentFormatter()
        val record = LogRecord(LogOrigin.Native, LogLevel.Info, "one-record", commitId = 1L)
        return Triple(
            LogSinkEvent.Start(formatter.formatHeader(LogDocumentIdentity("0".repeat(32), 1L, "DIAGNOSE"))),
            LogSinkEvent.Entry(record, formatter.formatRecord(record, LogFormatConfig(false))),
            LogSinkEvent.End(formatter.formatTrailer(RunSessionPhase.Done)),
        )
    }

    private fun factory(gateway: LogMediaStoreGateway): AndroidLogSinkFactory {
        val cache = InstrumentationRegistry.getInstrumentation().targetContext.cacheDir
        return AndroidLogSinkFactory(
            privateDirectory = java.io.File(cache, "sink-test-${System.nanoTime()}"),
            gateway = gateway,
            scope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
        )
    }

    /** Creates unopened storage sink instances just as the run adapter does. */
    private fun createSinks(
        factory: AndroidLogSinkFactory,
        request: LogSinkRequest,
        failures: MutableList<LogSinkFailure>,
    ): TestSinks {
        val bindings = factory.create(request, failures::add)
        return TestSinks(
            file = bindings.single { it.role == LogSinkRole.PRIVATE_FILE }.sink as LogFileSink,
            mediaStore = bindings.firstOrNull { it.role == LogSinkRole.MEDIA_STORE }?.sink,
        )
    }

    /** Test-only typed view of the two independently registered storage sinks. */
    private data class TestSinks(val file: LogFileSink, val mediaStore: LogSink?)

    private fun request() = LogSinkRequest(
        enabled = true, runId = "0".repeat(32), generation = 1L, backendKey = "DIAGNOSE",
        relativePath = "Download/ghostlock-log", formatConfig = LogFormatConfig(false),
    )

    private class FakeGateway(
        private val output: RecordingOutputStream?,
        blockOpen: Boolean = false,
    ) : LogMediaStoreGateway {
        val inserts = AtomicInteger(0)
        val deletes = AtomicInteger(0)
        val releaseOpen = CountDownLatch(if (blockOpen) 1 else 0)
        override fun insert(values: ContentValues): Uri? {
            inserts.incrementAndGet()
            return Uri.parse("content://ghostlock.test/log/1")
        }
        override fun open(uri: Uri): OutputStream? {
            releaseOpen.await(5L, TimeUnit.SECONDS)
            return output
        }
        override fun delete(uri: Uri): Int { deletes.incrementAndGet(); return 1 }
    }

    private class RecordingOutputStream : OutputStream() {
        private val bytes = ByteArrayOutputStream()
        val flushes = AtomicInteger(0)
        val closed = CountDownLatch(1)
        override fun write(value: Int) { synchronized(bytes) { bytes.write(value) } }
        override fun write(value: ByteArray, offset: Int, length: Int) {
            synchronized(bytes) { bytes.write(value, offset, length) }
        }
        override fun flush() { flushes.incrementAndGet() }
        override fun close() { closed.countDown() }
        fun bytes(): ByteArray = synchronized(bytes) { bytes.toByteArray() }
    }
}
