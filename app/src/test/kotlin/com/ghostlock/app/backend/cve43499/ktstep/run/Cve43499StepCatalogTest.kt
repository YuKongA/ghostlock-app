package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.OpsReply
import com.ghostlock.app.core.steps.StepInputs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class Cve43499StepCatalogTest {
    private val catalog = Cve43499StepCatalog()

    @Test
    fun selectsOnlyItsOwnStepsInRequestedOrder() {
        val (steps, refusal) = catalog.select(listOf("w3", "w1"))
        assertNull(refusal)
        assertEquals(listOf("w3", "w1"), steps.map { it.token })
        assertEquals(catalog.backendId, steps.first().backendId)
    }

    @Test
    fun rejectsForeignOrUnknownToken() {
        assertTrue(catalog.select(listOf("patch1")).second == "step-unknown:patch1")
        assertTrue(catalog.select(listOf("nope")).second == "step-unknown:nope")
    }

    @Test
    fun buildsNodesWithPerStepInputs() {
        val ops = object : OpsInvoker {
            override fun invoke(opName: String, values: Map<String, ULong>, blobs: Map<String, ByteArray>) =
                OpsReply("ok", null, emptyMap(), emptyMap())
        }
        val (steps, _) = catalog.select(listOf("w1"))
        val nodes = catalog.nodes(steps, StepInputs(selinuxAddress = 1uL), ops)
        assertEquals(1, nodes.size)
        assertEquals("w1", nodes.single().token)
    }
}
