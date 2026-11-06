package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationSpec
import com.ghostlock.app.domain.backend.BackendParameterRequirements
import com.ghostlock.app.domain.backend.BackendPresentationFact
import com.ghostlock.app.domain.backend.BackendPresentationIdentity
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Neutral configuration projection without concrete backend imports or preferences storage. */
class AndroidBackendConfigurationAdapterTest {
    /** Static facts and dynamic readiness are projected under the stable key. */
    @Test
    fun neutralSpecProjectsStaticFactsAndReadiness() {
        val adapter = AndroidBackendConfigurationAdapter(
            registryEntries = listOf(spec()),
            availability = {
                BackendAvailability.NotReady(
                    listOf(BackendAvailabilityReason("fixture-not-ready", "Fixture is not ready.")),
                )
            },
        )
        val state = adapter.states.value.getValue("TEST") as BackendConfiguration.Static
        assertEquals("value", state.values.first { value -> value.label == "fact" }.value)
        assertTrue(state.values.any { value -> value.value == "Fixture is not ready." })
    }

    /** Mutations fail closed until the profile-v3 editor port is connected. */
    @Test
    fun mutationsRemainUnsupported() {
        val adapter = AndroidBackendConfigurationAdapter(
            registryEntries = listOf(spec()),
            availability = { BackendAvailability.Ready() },
        )
        assertEquals(BackendConfigMutationResult.UNSUPPORTED, adapter.setBoolean("TEST", "flag", true))
        assertEquals(BackendConfigMutationResult.UNSUPPORTED, adapter.selectValue("TEST", "choice", "a"))
        assertEquals(BackendConfigMutationResult.UNSUPPORTED, adapter.importDocument("TEST", "doc", "uri"))
    }

    private fun spec(): BackendSpec = BackendSpec(
        key = "TEST",
        presentation = BackendPresentationIdentity(
            categoryLabel = "Test",
            displayName = "Test backend",
            facts = listOf(BackendPresentationFact("fact", "value")),
        ),
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = ExecutionDomain.APP,
        configuration = BackendConfigurationSpec(),
        parameterRequirements = BackendParameterRequirements.EMPTY,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )
}
