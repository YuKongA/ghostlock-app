package com.ghostlock.app.data.runhost

import android.os.Binder
import android.os.DeadObjectException
import android.os.IBinder
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostSnapshotModel
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.data.runhost.client.RunHostClientOutcome
import com.ghostlock.app.data.runhost.client.RunHostClientTestLatches
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.data.runhost.client.ShizukuRunHostBinding
import com.ghostlock.app.data.runhost.client.ShizukuRunHostClient
import com.ghostlock.app.data.runhost.wire.toWire
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshotReply
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Host-death mapping remains an explicit transport failure, never a product report. */
@RunWith(AndroidJUnit4::class)
class RunHostHostDeathTest {
    /** Binder death during callback registration maps to HOST_DIED. */
    @Test
    fun deadBinderCannotProduceTerminalReport() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val host = object : IRunHost.Stub() {
                override fun setCallback(runId: String, generation: Long, callback: IRunHostCallback): Int =
                    throw DeadObjectException()

                override fun prepare(request: RunHostRequest): Int = RunHostCode.BAD_STATE.wireValue
                override fun start(runId: String, generation: Long): Int = RunHostCode.BAD_STATE.wireValue
                override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply =
                    RunHostSnapshotReply().also { it.code = RunHostCode.HOST_DIED.wireValue }

                override fun cancel(runId: String, generation: Long): Int = RunHostCode.HOST_DIED.wireValue
                override fun requestClose(runId: String, generation: Long): Int = RunHostCode.HOST_DIED.wireValue
                override fun destroy() = Unit
            }
            var closed = false
            val client = ShizukuRunHostClient(
                object : ShizukuRunHostBinding {
                    override suspend fun bind(): IRunHost = host
                    override fun close(host: IRunHost) {
                        closed = true
                    }
                },
            )
            val connector = RunHostLogConnector(
                RunHostIdentity(fixture.request.runId, fixture.request.generation),
            )
            val collection = async { connector.collect { } }
            val outcome = client.run(fixture.request, connector)
            assertTrue(outcome is RunHostClientOutcome.TransportFailure)
            assertEquals(
                RunHostCode.HOST_DIED,
                (outcome as RunHostClientOutcome.TransportFailure).code,
            )
            assertTrue(collection.await() is LogConnectorEnd.Incomplete)
            assertTrue(closed)
        }
    }

    /** A valid terminal is returned only after close/destroy and the expected physical binder death. */
    @Test
    fun terminalWaitsForExpectedBinderDeath() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val events = CopyOnWriteArrayList<String>()
            val bindingClosed = CountDownLatch(1)
            val binder = ControllableBinder { events += "binder-death" }
            val host = TerminalHost(
                binder = binder,
                snapshot = terminalSnapshot(fixture.request),
                onTerminal = { events += "terminal-callback" },
                onRequestClose = { events += "request-close" },
                onDestroy = {
                    events += "destroy"
                    Thread(
                        {
                            if (bindingClosed.await(2L, TimeUnit.SECONDS)) binder.die()
                        },
                        "runhost-test-delayed-death",
                    ).start()
                },
            )
            val client = ShizukuRunHostClient(
                object : ShizukuRunHostBinding {
                    override suspend fun bind(): IRunHost = host

                    override fun close(host: IRunHost) {
                        events += "binding-close"
                        bindingClosed.countDown()
                    }
                },
                RunHostClientTestLatches(
                    afterTerminalDrainBeforeClose = { events += "terminal-drained" },
                ),
            )

            val (outcome, logEnd) = runClient(client, fixture.request)
            events += "client-return"

            assertTrue(outcome is RunHostClientOutcome.Terminal)
            assertEquals(LogConnectorEnd.Drained, logEnd)
            assertFalse(binder.isBinderAlive)
            assertEquals(
                listOf(
                    "terminal-callback",
                    "terminal-drained",
                    "request-close",
                    "destroy",
                    "binding-close",
                    "binder-death",
                    "client-return",
                ),
                events.toList(),
            )
        }
    }

    /** Binding release cannot masquerade as death; timeout quarantines until a late death arrives. */
    @Test
    fun bindingCloseWithoutDeathTimesOutAndQuarantines() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val firstBinder = ControllableBinder()
            var host: IRunHost = TerminalHost(firstBinder, terminalSnapshot(fixture.request))
            var bindCount = 0
            var closeCount = 0
            val client = ShizukuRunHostClient(
                object : ShizukuRunHostBinding {
                    override suspend fun bind(): IRunHost {
                        bindCount += 1
                        return host
                    }

                    override fun close(host: IRunHost) {
                        closeCount += 1
                    }
                },
            )

            val (timedOut, firstEnd) = withTimeout(8_000L) {
                runClient(client, fixture.request)
            }
            assertEquals(
                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                (timedOut as RunHostClientOutcome.TransportFailure).code,
            )
            assertEquals(LogConnectorEnd.Drained, firstEnd)
            assertTrue(firstBinder.isBinderAlive)
            assertEquals(1, bindCount)
            assertEquals(1, closeCount)

            val (quarantined, secondEnd) = runClient(client, fixture.request)
            assertEquals(
                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                (quarantined as RunHostClientOutcome.TransportFailure).code,
            )
            assertTrue(secondEnd is LogConnectorEnd.Incomplete)
            assertEquals(1, bindCount)
            assertEquals(1, closeCount)

            firstBinder.die()
            val recoveredBinder = ControllableBinder()
            host = RejectingHost(recoveredBinder, dieOnDestroy = true)
            val (recovered, thirdEnd) = runClient(client, fixture.request)
            assertEquals(
                RunHostCode.INVALID_REQUEST,
                (recovered as RunHostClientOutcome.Rejected).code,
            )
            assertTrue(thirdEnd is LogConnectorEnd.Incomplete)
            assertEquals(2, bindCount)
            assertEquals(2, closeCount)
            assertFalse(recoveredBinder.isBinderAlive)
        }
    }

    private suspend fun runClient(
        client: ShizukuRunHostClient,
        request: RunHostRequest,
    ): Pair<RunHostClientOutcome, LogConnectorEnd> = coroutineScope {
        val connector = RunHostLogConnector(RunHostIdentity(request.runId, request.generation))
        val collection = async { connector.collect { } }
        client.run(request, connector) to collection.await()
    }

    /** Fake host that publishes one valid terminal snapshot and delegates teardown effects. */
    private class TerminalHost(
        private val binder: ControllableBinder,
        private val snapshot: RunHostSnapshotModel,
        private val onTerminal: () -> Unit = {},
        private val onRequestClose: () -> Unit = {},
        private val onDestroy: () -> Unit = {},
    ) : IRunHost {
        private lateinit var callback: IRunHostCallback

        override fun asBinder(): IBinder = binder

        override fun setCallback(
            runId: String,
            generation: Long,
            callback: IRunHostCallback,
        ): Int {
            this.callback = callback
            return RunHostCode.OK.wireValue
        }

        override fun prepare(request: RunHostRequest): Int = RunHostCode.OK.wireValue

        override fun start(runId: String, generation: Long): Int {
            onTerminal()
            callback.onTerminal(snapshot.toWire())
            return RunHostCode.OK.wireValue
        }

        override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply =
            RunHostSnapshotReply().also { reply ->
                reply.code = RunHostCode.OK.wireValue
                reply.snapshot = snapshot.toWire()
            }

        override fun cancel(runId: String, generation: Long): Int = RunHostCode.BAD_STATE.wireValue

        override fun requestClose(runId: String, generation: Long): Int {
            onRequestClose()
            return RunHostCode.OK.wireValue
        }

        override fun destroy() = onDestroy()
    }

    /** Rejecting recovery host whose authorized destroy can produce the expected binder death. */
    private class RejectingHost(
        private val binder: ControllableBinder,
        private val dieOnDestroy: Boolean,
    ) : IRunHost {
        override fun asBinder(): IBinder = binder

        override fun setCallback(
            runId: String,
            generation: Long,
            callback: IRunHostCallback,
        ): Int = RunHostCode.OK.wireValue

        override fun prepare(request: RunHostRequest): Int = RunHostCode.INVALID_REQUEST.wireValue

        override fun start(runId: String, generation: Long): Int = RunHostCode.BAD_STATE.wireValue

        override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply =
            RunHostSnapshotReply().also { it.code = RunHostCode.BAD_STATE.wireValue }

        override fun cancel(runId: String, generation: Long): Int = RunHostCode.BAD_STATE.wireValue

        override fun requestClose(runId: String, generation: Long): Int = RunHostCode.OK.wireValue

        override fun destroy() {
            if (dieOnDestroy) binder.die()
        }
    }

    /** Binder test double with explicit liveness and late-death delivery. */
    private class ControllableBinder(
        private val onDie: () -> Unit = {},
    ) : Binder() {
        private val recipients = CopyOnWriteArrayList<IBinder.DeathRecipient>()

        @Volatile
        private var alive = true

        override fun isBinderAlive(): Boolean = alive

        override fun pingBinder(): Boolean = alive

        override fun linkToDeath(recipient: IBinder.DeathRecipient, flags: Int) {
            if (!alive) throw DeadObjectException()
            recipients += recipient
        }

        override fun unlinkToDeath(recipient: IBinder.DeathRecipient, flags: Int): Boolean =
            recipients.remove(recipient)

        fun die() {
            if (!alive) return
            alive = false
            onDie()
            val current = recipients.toList()
            recipients.clear()
            current.forEach(IBinder.DeathRecipient::binderDied)
        }
    }

    /** Returns one valid terminal diagnostic snapshot for the fake transport. */
    private fun terminalSnapshot(request: RunHostRequest): RunHostSnapshotModel = RunHostSnapshotModel(
        identity = RunHostIdentity(request.runId, request.generation),
        state = RunHostState.TERMINAL,
        lastCode = RunHostCode.OK,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
        diagnosticOutcome = DiagnosticOutcome.PASS,
        backendPrimitive = StageResult.SUCCESS,
        scriptDispatch = StageResult.SUCCESS,
        payloadItems = emptyList(),
        payloadOverall = StageResult.SKIPPED,
        finishItems = emptyList(),
        finishOverall = StageResult.SKIPPED,
        backendCleanup = StageResult.SUCCESS,
        terminal = RunTerminal.DIAGNOSTIC_COMPLETE,
        logSequence = 0L,
        droppedLogs = 0L,
    )
}
