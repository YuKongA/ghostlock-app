package com.ghostlock.app.data.runhost

import android.os.Binder
import android.os.DeadObjectException
import android.os.IBinder
import android.os.Process
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.runhost.client.AppRunHostClient
import com.ghostlock.app.data.runhost.client.RunHostClientOutcome
import com.ghostlock.app.data.runhost.client.RunHostClientTestLatches
import com.ghostlock.app.data.runhost.client.RunHostLogConnector
import com.ghostlock.app.data.runhost.client.ShizukuRunHostBinding
import com.ghostlock.app.data.runhost.client.ShizukuRunHostClient
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshotReply
import java.util.concurrent.CopyOnWriteArrayList
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device lifecycle coverage for one-shot plugin hosts, rejection cleanup, and quarantine. */
@RunWith(AndroidJUnit4::class)
class PluginRunHostProcessLifecycleTest {
    /** Two accepted App runs each complete only after their one-shot binder has physically died. */
    @Test
    fun consecutiveAppRunsUseFreshOneShotHosts() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val client = AppRunHostClient(context)
        repeat(2) {
            RunHostTestFixture.create(context).use { fixture ->
                val (outcome, logEnd) = withTimeout(30_000L) {
                    runApp(client, fixture.request)
                }
                assertTrue(outcome is RunHostClientOutcome.Terminal)
                assertEquals(LogConnectorEnd.Drained, logEnd)
            }
        }
    }

    /** A real prepare rejection still tears down its host, so the same client can run again. */
    @Test
    fun prepareRejectDoesNotLeaveAnAppHostReusable() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val client = AppRunHostClient(context)
        RunHostTestFixture.create(context).use { rejected ->
            rejected.request.pluginContentSha256 = "0".repeat(64)
            val (outcome, logEnd) = withTimeout(30_000L) {
                runApp(client, rejected.request)
            }
            assertTrue(outcome is RunHostClientOutcome.Rejected)
            assertTrue(logEnd is LogConnectorEnd.Incomplete)
        }
        RunHostTestFixture.create(context).use { valid ->
            val (outcome, logEnd) = withTimeout(30_000L) {
                runApp(client, valid.request)
            }
            assertTrue(outcome is RunHostClientOutcome.Terminal)
            assertEquals(LogConnectorEnd.Drained, logEnd)
        }
    }

    /** A live binder that misses teardown is quarantined until its late death is observed. */
    @Test
    fun teardownTimeoutQuarantinesUntilLateBinderDeath() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val firstBinder = ControllableBinder()
        var host: IRunHost = RejectingHost(firstBinder, dieOnDestroy = false)
        var bindCount = 0
        val client = ShizukuRunHostClient(
            object : ShizukuRunHostBinding {
                override suspend fun bind(): IRunHost {
                    bindCount += 1
                    return host
                }

                override fun close(host: IRunHost) = Unit
            },
        )
        RunHostTestFixture.create(context).use { fixture ->
            val (timedOut, firstEnd) = withTimeout(8_000L) {
                runShizuku(client, fixture.request)
            }
            assertEquals(
                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                (timedOut as RunHostClientOutcome.TransportFailure).code,
            )
            assertTrue(firstEnd is LogConnectorEnd.Incomplete)
            assertEquals(1, bindCount)

            val (quarantined, secondEnd) = runShizuku(client, fixture.request)
            assertEquals(
                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                (quarantined as RunHostClientOutcome.TransportFailure).code,
            )
            assertTrue(secondEnd is LogConnectorEnd.Incomplete)
            assertEquals(1, bindCount)

            firstBinder.die()
            val recoveredBinder = ControllableBinder()
            host = RejectingHost(recoveredBinder, dieOnDestroy = true)
            val (recovered, thirdEnd) = runShizuku(client, fixture.request)
            assertEquals(RunHostCode.INVALID_REQUEST, (recovered as RunHostClientOutcome.Rejected).code)
            assertTrue(thirdEnd is LogConnectorEnd.Incomplete)
            assertEquals(2, bindCount)
            assertTrue(!recoveredBinder.isBinderAlive)
        }
    }

    /**
     * External shell-only gate: killing the instrumentation process after bind must interrupt adb
     * instrumentation rather than produce a JUnit PASS. Normal suites skip this method.
     */
    @Test
    fun externalGateKillsMainProcessAfterRunHostBind(): Unit = runBlocking {
        assumeTrue(
            "run with -e $KILL_MAIN_ARGUMENT true; success is instrumentation connection loss",
            InstrumentationRegistry.getArguments().getString(KILL_MAIN_ARGUMENT) == "true",
        )
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        RunHostTestFixture.create(context).use { fixture ->
            val client = AppRunHostClient(
                context,
                RunHostClientTestLatches(
                    afterBindBeforeCallback = { Process.killProcess(Process.myPid()) },
                ),
            )
            runApp(client, fixture.request)
            error("instrumentation survived the external kill-main gate")
        }
    }

    private suspend fun runApp(
        client: AppRunHostClient,
        request: RunHostRequest,
    ): Pair<RunHostClientOutcome, LogConnectorEnd> = coroutineScope {
        val connector = RunHostLogConnector(RunHostIdentity(request.runId, request.generation))
        val collection = async { connector.collect { } }
        client.run(request, connector) to collection.await()
    }

    private suspend fun runShizuku(
        client: ShizukuRunHostClient,
        request: RunHostRequest,
    ): Pair<RunHostClientOutcome, LogConnectorEnd> = coroutineScope {
        val connector = RunHostLogConnector(RunHostIdentity(request.runId, request.generation))
        val collection = async { connector.collect { } }
        client.run(request, connector) to collection.await()
    }

    /** Minimal rejecting host whose binder death can be delayed past the client timeout. */
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

    /** Binder test double with explicit late-death delivery and real liveness transitions. */
    private class ControllableBinder : Binder() {
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
            val current = recipients.toList()
            recipients.clear()
            current.forEach(IBinder.DeathRecipient::binderDied)
        }
    }

    private companion object {
        const val KILL_MAIN_ARGUMENT = "ghostlock.runhost.kill-main-after-bind"
    }
}
