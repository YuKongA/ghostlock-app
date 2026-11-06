package com.ghostlock.app.backend.transport

import android.os.DeadObjectException
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshotReply
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
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
            val outcome = client.run(fixture.request)
            assertTrue(outcome is RunHostClientOutcome.TransportFailure)
            assertEquals(
                RunHostCode.HOST_DIED,
                (outcome as RunHostClientOutcome.TransportFailure).code,
            )
            assertTrue(closed)
        }
    }
}
