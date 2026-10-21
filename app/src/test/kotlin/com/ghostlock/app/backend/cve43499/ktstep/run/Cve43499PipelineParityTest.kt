package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.engine.Overall
import com.ghostlock.app.core.engine.PipelineEngine
import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.OpsReply
import com.ghostlock.app.core.steps.StepInputs
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499PipelineParityTest {
    private class FakeOps(private val status: String) : OpsInvoker {
        override fun invoke(opName: String, values: Map<String, ULong>, blobs: Map<String, ByteArray>) =
            OpsReply(status, if (status == "ok") null else status, emptyMap(), emptyMap())
    }

    private fun inputs() = StepInputs(
        selinuxAddress = 1uL, credentialAddress = 2uL, credentialValue = 3uL, seccompAddress = 4uL,
    )

    @Test
    fun successMatchesStepEngine() = runBlocking {
        val inputs = inputs()
        val catalog = Cve43499StepCatalog()
        val (selected, _) = catalog.select(listOf("w1", "w2", "w3"))
        val pipeline = PipelineEngine().run(catalog.nodes(selected, inputs, FakeOps("ok")))
        assertTrue(pipeline.outcomes.all { it.result is RunNodeResult.Done })
        assertEquals(Overall.Ok, pipeline.overall)
    }

    @Test
    fun refusalAndFrozenMatchShape() = runBlocking {
        val missing = StepInputs()
        val catalog = Cve43499StepCatalog()
        val (selected, _) = catalog.select(listOf("w1"))
        val newMissing = PipelineEngine().run(catalog.nodes(selected, missing, FakeOps("ok")))
        assertTrue(newMissing.primary is RunNodeResult.Refused)

        val (all, _) = catalog.select(listOf("w1", "w2", "w3"))
        val frozenNew = PipelineEngine().run(catalog.nodes(all, inputs(), FakeOps("indeterminate")))
        assertEquals(3, frozenNew.outcomes.size)
        assertTrue(frozenNew.outcomes.all { it.result is RunNodeResult.Frozen })
    }
}
