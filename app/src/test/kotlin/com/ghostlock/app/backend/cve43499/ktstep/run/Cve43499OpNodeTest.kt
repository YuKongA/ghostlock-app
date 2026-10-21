package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.core.engine.SkipPolicy
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.OpsReply
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepInputs
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499OpNodeTest {
    private class FakeOps(private val reply: OpsReply) : OpsInvoker {
        val names = mutableListOf<String>()
        override fun invoke(opName: String, values: Map<String, ULong>, blobs: Map<String, ByteArray>): OpsReply {
            names += opName
            return reply
        }
    }

    private fun ok() = OpsReply("ok", null, emptyMap(), emptyMap())

    @Test
    fun missingInputRefusesWithoutCall() = runBlocking {
        val ops = FakeOps(ok())
        val node = Cve43499OpNode("w1", "w1", StepId.W1, StepInputs(), ops)
        val result = node.run()
        assertTrue(result is RunNodeResult.Refused)
        assertEquals("precondition:w1-address", (result as RunNodeResult.Refused).reason)
        assertTrue(ops.names.isEmpty())
    }

    @Test
    fun readyCallsOpsAndDone() = runBlocking {
        val ops = FakeOps(ok())
        val node = Cve43499OpNode("w1", "w1", StepId.W1, StepInputs(selinuxAddress = 1uL), ops)
        val result = node.run()
        assertEquals(RunNodeResult.Done(), result)
        assertEquals(listOf("write_word"), node.calls())
        assertEquals(SkipPolicy.FrozenRemaining, node.policy.skipPolicy)
    }

    @Test
    fun notReadyIsRetryableAndIndeterminateIsFrozen() = runBlocking {
        val retry = Cve43499OpNode("w1", "w1", StepId.W1, StepInputs(selinuxAddress = 1uL),
            FakeOps(OpsReply("not-ready", "busy", emptyMap(), emptyMap())))
        assertTrue(retry.run() is RunNodeResult.Retryable)

        val frozen = Cve43499OpNode("w1", "w1", StepId.W1, StepInputs(selinuxAddress = 1uL),
            FakeOps(OpsReply("indeterminate", "unknown", emptyMap(), emptyMap())))
        assertTrue(frozen.run() is RunNodeResult.Frozen)

        val refused = Cve43499OpNode("w1", "w1", StepId.W1, StepInputs(selinuxAddress = 1uL),
            FakeOps(OpsReply("failed", "no", emptyMap(), emptyMap())))
        assertTrue(refused.run() is RunNodeResult.Refused)
    }
}
