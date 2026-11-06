package com.ghostlock.app.data.run

import android.content.ContentValues
import android.net.Uri
import android.provider.MediaStore
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.domain.run.RunLogFormatConfig
import com.ghostlock.app.domain.run.RunLogSink
import com.ghostlock.app.domain.run.RunLogSinkEmitResult
import com.ghostlock.app.domain.run.RunLogSinkFailure
import com.ghostlock.app.domain.run.RunLogSinkFailureKind
import com.ghostlock.app.domain.run.RunLogSinkRequest
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Locks immediate creation, first-failure precedence, FIFO prefix writes, and worker-only close ownership. */
@RunWith(AndroidJUnit4::class)
class AndroidRunLogSinkFactoryTest {
    /** Successful output flushes each row, finishes once, and rejects late rows without a warning. */
    @Test
    fun activeSinkWritesRowsAndClosesExactlyOnce() = runBlocking {
        val output = RecordingOutputStream()
        val gateway = FakeGateway(output = output)
        val failure = FailureRecorder()
        val sink = factory(gateway).create(request(), failure::record)

        assertEquals(
            RunLogSinkEmitResult.ACCEPTED,
            sink.tryEmit(listOf("GLLOG1\n".toByteArray(), "end\tcomplete=true\n".toByteArray())),
        )
        sink.finish()
        assertEquals(RunLogSinkEmitResult.REJECTED, sink.tryEmit(listOf("late\n".toByteArray())))
        assertTrue(output.closeObserved.await(2L, TimeUnit.SECONDS))

        assertEquals(0, failure.count.get())
        assertEquals("GLLOG1\nend\tcomplete=true\n", output.bytes.toString(Charsets.UTF_8))
        assertEquals(2, output.flushCount.get())
        assertEquals(1, output.closeCount.get())
        assertEquals(0, gateway.deleteCount.get())
        assertEquals("Download/ghostlock-log", gateway.lastPathPrefix())
        assertEquals(
            "ghostlock-run-${request().runId}-g${request().generation}.log.txt",
            gateway.lastDisplayName(),
        )
    }

    /** Factory creation returns while open is blocked; finish drains the accepted prefix after open resumes. */
    @Test
    fun createReturnsBeforeBlockedOpenAndFinishStillDrainsPrefix() = runBlocking {
        val output = RecordingOutputStream()
        val gateway = FakeGateway(output = output, blockOpen = true)
        val failure = FailureRecorder()

        val sink = factory(gateway).create(request(), failure::record)
        assertTrue(gateway.openStarted.await(2L, TimeUnit.SECONDS))
        assertEquals(RunLogSinkEmitResult.ACCEPTED, sink.tryEmit(listOf("prefix\n".toByteArray())))
        sink.finish()
        assertEquals(0, failure.count.get())
        assertEquals(0, gateway.deleteCount.get())
        assertEquals(0, output.closeCount.get())
        gateway.releaseOpen.countDown()
        assertTrue(output.closeObserved.await(2L, TimeUnit.SECONDS))

        assertEquals("prefix\n", output.bytes.toString(Charsets.UTF_8))
        assertEquals(0, failure.count.get())
        assertEquals(1, output.closeCount.get())
    }

    /** Insert success followed by open failure deletes the one orphan URI and reports OPEN once. */
    @Test
    fun openFailureDeletesInsertedUriAndReportsOnce() = runBlocking {
        val gateway = FakeGateway(output = null)
        val failure = FailureRecorder()

        factory(gateway).create(request(), failure::record)
        assertTrue(failure.observed.await(2L, TimeUnit.SECONDS))
        assertTrue(gateway.deleteObserved.await(2L, TimeUnit.SECONDS))

        assertEquals(RunLogSinkFailureKind.OPEN, failure.first.get()?.kind)
        assertEquals(1, failure.count.get())
        assertEquals(1, gateway.deleteCount.get())

        val insertFailureGateway = FakeGateway(
            output = RecordingOutputStream(),
            failInsert = true,
        )
        val insertFailure = FailureRecorder()
        factory(insertFailureGateway).create(request(), insertFailure::record)
        assertTrue(insertFailure.observed.await(2L, TimeUnit.SECONDS))
        assertEquals(RunLogSinkFailureKind.OPEN, insertFailure.first.get()?.kind)
        assertEquals(1, insertFailure.count.get())
        assertEquals(1, insertFailureGateway.insertCount.get())
        assertEquals(0, insertFailureGateway.deleteCount.get())
    }

    /** Invalid destination fails before insert and therefore has no URI cleanup. */
    @Test
    fun invalidPathFailsBeforeMediaStoreInsert() = runBlocking {
        val gateway = FakeGateway(output = RecordingOutputStream())
        val failure = FailureRecorder()

        factory(gateway).create(
            request().copy(relativePath = "Pictures/ghostlock-log"),
            failure::record,
        )
        assertTrue(failure.observed.await(2L, TimeUnit.SECONDS))

        assertEquals(RunLogSinkFailureKind.OPEN, failure.first.get()?.kind)
        assertEquals(0, gateway.insertCount.get())
        assertEquals(0, gateway.deleteCount.get())
    }

    /** Queue-full remains first while a late open either drains the prefix or cleans its orphan URI. */
    @Test
    fun queueFullBeforeOpenFailureKeepsFirstFailureAndCleansUri() = runBlocking {
        val successOutput = RecordingOutputStream()
        val successGateway = FakeGateway(output = successOutput, blockOpen = true)
        val successFailure = FailureRecorder()
        val successSink = factory(successGateway).create(request(), successFailure::record)
        assertTrue(successGateway.openStarted.await(2L, TimeUnit.SECONDS))

        val expectedPrefix = ByteArrayOutputStream()
        repeat(COMMAND_CAPACITY) { index ->
            val row = "row-$index\n".toByteArray()
            expectedPrefix.write(row)
            assertEquals(
                RunLogSinkEmitResult.ACCEPTED,
                successSink.tryEmit(listOf(row)),
            )
        }
        assertEquals(
            RunLogSinkEmitResult.REJECTED,
            successSink.tryEmit(listOf("overflow\n".toByteArray())),
        )
        assertTrue(successFailure.observed.await(2L, TimeUnit.SECONDS))
        assertEquals(RunLogSinkFailureKind.QUEUE_FULL, successFailure.first.get()?.kind)
        assertEquals(0, successOutput.closeCount.get())

        successGateway.releaseOpen.countDown()
        assertTrue(successOutput.closeObserved.await(2L, TimeUnit.SECONDS))
        assertArrayEquals(expectedPrefix.toByteArray(), successOutput.bytes.toByteArray())
        assertEquals(1, successFailure.count.get())
        assertEquals(1, successOutput.closeCount.get())
        assertEquals(0, successGateway.deleteCount.get())

        val failureGateway = FakeGateway(output = null, blockOpen = true)
        val lateOpenFailure = FailureRecorder()
        val failureSink = factory(failureGateway).create(request(), lateOpenFailure::record)
        assertTrue(failureGateway.openStarted.await(2L, TimeUnit.SECONDS))
        repeat(COMMAND_CAPACITY) { index ->
            assertEquals(
                RunLogSinkEmitResult.ACCEPTED,
                failureSink.tryEmit(listOf("failure-row-$index\n".toByteArray())),
            )
        }
        assertEquals(
            RunLogSinkEmitResult.REJECTED,
            failureSink.tryEmit(listOf("failure-overflow\n".toByteArray())),
        )
        assertTrue(lateOpenFailure.observed.await(2L, TimeUnit.SECONDS))
        assertEquals(RunLogSinkFailureKind.QUEUE_FULL, lateOpenFailure.first.get()?.kind)

        failureGateway.releaseOpen.countDown()
        assertTrue(failureGateway.deleteObserved.await(2L, TimeUnit.SECONDS))
        assertEquals(1, lateOpenFailure.count.get())
        assertEquals(RunLogSinkFailureKind.QUEUE_FULL, lateOpenFailure.first.get()?.kind)
        assertEquals(1, failureGateway.deleteCount.get())
    }

    /** Write/flush failures outrank later close failures, while a close-only failure reports CLOSE. */
    @Test
    fun firstFailureWinsAcrossWriteFlushAndClose() = runBlocking {
        val writeOutput = RecordingOutputStream(failAfterWrites = 1, failOnClose = true)
        val writeRun = runFailingSink(writeOutput) {
            listOf("first\n".toByteArray(), "second\n".toByteArray())
        }
        assertEquals(RunLogSinkFailureKind.WRITE, writeRun.failure.first.get()?.kind)
        assertEquals(1, writeRun.failure.count.get())
        assertEquals(
            RunLogSinkEmitResult.REJECTED,
            writeRun.sink.tryEmit(listOf("late-after-failure\n".toByteArray())),
        )
        assertEquals(1, writeRun.failure.count.get())

        val flushOutput = RecordingOutputStream(failOnFlushCall = 1, failOnClose = true)
        val flushRun = runFailingSink(flushOutput) { listOf("row\n".toByteArray()) }
        assertEquals(RunLogSinkFailureKind.FLUSH, flushRun.failure.first.get()?.kind)
        assertEquals(1, flushRun.failure.count.get())

        val closeOutput = RecordingOutputStream(failOnClose = true)
        val closeRun = runFailingSink(closeOutput) { listOf("row\n".toByteArray()) }
        assertEquals(RunLogSinkFailureKind.CLOSE, closeRun.failure.first.get()?.kind)
        assertEquals(1, closeRun.failure.count.get())
    }

    /** A provider may expose part of a failing row; the visible file remains a canonical byte prefix. */
    @Test
    fun partialWriteFailureAllowsIncompleteFinalRow() = runBlocking {
        val output = RecordingOutputStream(
            partialFailureWrite = 2,
            partialBytesBeforeFailure = 3,
        )
        val failure = FailureRecorder()
        val sink = factory(FakeGateway(output)).create(request(), failure::record)
        assertEquals(
            RunLogSinkEmitResult.ACCEPTED,
            sink.tryEmit(listOf("GLLOG1\n".toByteArray(), "entry\n".toByteArray())),
        )
        sink.finish()
        assertTrue(failure.observed.await(2L, TimeUnit.SECONDS))
        assertTrue(output.closeObserved.await(2L, TimeUnit.SECONDS))

        assertEquals(RunLogSinkFailureKind.WRITE, failure.first.get()?.kind)
        val canonical = "GLLOG1\nentry\n".toByteArray()
        val visible = output.bytes.toByteArray()
        assertArrayEquals(canonical.copyOf(visible.size), visible)
        assertEquals("GLLOG1\nent", visible.toString(Charsets.UTF_8))
        assertFalse(visible.last() == LF_BYTE)
        assertEquals(1, output.flushCount.get())
        assertEquals(1, output.closeCount.get())
    }

    /** Runs one sink to its worker close and returns both the sink and first-failure recorder. */
    private fun runFailingSink(
        output: RecordingOutputStream,
        rows: () -> List<ByteArray>,
    ): FailingSinkRun {
        val failure = FailureRecorder()
        val sink = factory(FakeGateway(output)).create(request(), failure::record)
        assertEquals(RunLogSinkEmitResult.ACCEPTED, sink.tryEmit(rows()))
        sink.finish()
        assertTrue(failure.observed.await(2L, TimeUnit.SECONDS))
        assertTrue(output.closeObserved.await(2L, TimeUnit.SECONDS))
        return FailingSinkRun(sink, failure)
    }

    /** Test observation for one failed worker lifecycle. */
    private data class FailingSinkRun(
        val sink: RunLogSink,
        val failure: FailureRecorder,
    )

    /** @return Factory with an independent test IO supervisor. */
    private fun factory(gateway: RunLogMediaStoreGateway): AndroidRunLogSinkFactory =
        AndroidRunLogSinkFactory(
            gateway = gateway,
            scope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
        )

    /** @return Valid frozen request with a unique run/generation display name. */
    private fun request(): RunLogSinkRequest = RunLogSinkRequest(
        enabled = true,
        runId = "0123456789abcdef0123456789abcdef",
        generation = 3L,
        backendKey = "backend",
        relativePath = "Download/ghostlock-log",
        formatConfig = RunLogFormatConfig(showTimestamp = false),
    )

    /** Records every callback while retaining the first failure value. */
    private class FailureRecorder {
        val count = AtomicInteger(0)
        val first = AtomicReference<RunLogSinkFailure?>(null)
        val observed = CountDownLatch(1)

        fun record(failure: RunLogSinkFailure) {
            count.incrementAndGet()
            first.compareAndSet(null, failure)
            observed.countDown()
        }
    }

    /** Deterministic fake MediaStore gateway. */
    private class FakeGateway(
        private val output: OutputStream?,
        private val blockOpen: Boolean = false,
        private val failInsert: Boolean = false,
    ) : RunLogMediaStoreGateway {
        private val uri = Uri.parse("content://ghostlock.test/download/1")
        private var values: ContentValues? = null
        val insertCount = AtomicInteger(0)
        val deleteCount = AtomicInteger(0)
        val deleteObserved = CountDownLatch(1)
        val openStarted = CountDownLatch(1)
        val releaseOpen = CountDownLatch(1)

        override fun insert(values: ContentValues): Uri? {
            insertCount.incrementAndGet()
            this.values = ContentValues(values)
            return if (failInsert) null else uri
        }

        override fun open(uri: Uri): OutputStream? {
            openStarted.countDown()
            if (blockOpen) assertTrue(releaseOpen.await(2L, TimeUnit.SECONDS))
            return output
        }

        override fun delete(uri: Uri): Int {
            deleteCount.incrementAndGet()
            deleteObserved.countDown()
            return 1
        }

        fun lastPathPrefix(): String =
            values?.getAsString(MediaStore.MediaColumns.RELATIVE_PATH)?.substringBeforeLast('/') ?: ""

        fun lastDisplayName(): String =
            values?.getAsString(MediaStore.MediaColumns.DISPLAY_NAME) ?: ""
    }

    /** Output stream with deterministic write/flush/close failure injection. */
    private class RecordingOutputStream(
        private val failAfterWrites: Int = Int.MAX_VALUE,
        private val partialFailureWrite: Int = Int.MAX_VALUE,
        private val partialBytesBeforeFailure: Int = 0,
        private val failOnFlushCall: Int = Int.MAX_VALUE,
        private val failOnClose: Boolean = false,
    ) : OutputStream() {
        val bytes = ByteArrayOutputStream()
        val flushCount = AtomicInteger(0)
        val closeCount = AtomicInteger(0)
        val closeObserved = CountDownLatch(1)
        private val writeCount = AtomicInteger(0)

        override fun write(value: Int) {
            write(byteArrayOf(value.toByte()))
        }

        override fun write(buffer: ByteArray, offset: Int, length: Int) {
            val call = writeCount.incrementAndGet()
            if (call > failAfterWrites) error("forced write failure")
            if (call == partialFailureWrite) {
                val partialLength = partialBytesBeforeFailure.coerceIn(0, length)
                bytes.write(buffer, offset, partialLength)
                error("forced partial write failure")
            }
            bytes.write(buffer, offset, length)
        }

        override fun flush() {
            val call = flushCount.incrementAndGet()
            if (call == failOnFlushCall) error("forced flush failure")
        }

        override fun close() {
            closeCount.incrementAndGet()
            closeObserved.countDown()
            if (failOnClose) error("forced close failure")
        }
    }

    private companion object {
        const val COMMAND_CAPACITY: Int = 512
        const val LF_BYTE: Byte = 10
    }
}
