package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationPort
import com.ghostlock.app.domain.backend.BackendSpec
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Android composition adapter for neutral BackendSpec presentation/configuration facts.
 *
 * @param registryEntries Immutable registry membership.
 * @param availability Dynamic resource-free readiness resolver.
 * @param presentation Neutral presentation projector.
 * @note Configuration writes are fail-closed until a version-three profile editor port is connected; this
 * adapter never recreates the retired SharedPreferences/backend-callback authority.
 */
class AndroidBackendConfigurationAdapter(
    registryEntries: List<BackendSpec>,
    private val availability: (String) -> BackendAvailability?,
    private val presentation: BackendPresentationAdapter = BackendPresentationAdapter(),
) : BackendConfigurationPort {
    private val entriesByKey = registryEntries.associateBy(BackendSpec::key)
    private val mutableStates = MutableStateFlow(buildStates())

    override val states: StateFlow<Map<String, BackendConfiguration>> = mutableStates.asStateFlow()

    /** Reprojects one neutral static/readiness card. */
    override fun refresh(key: String) {
        val spec = entriesByKey[key] ?: return
        val current = availability(key) ?: missingAvailability()
        mutableStates.value = mutableStates.value + (key to presentation.project(spec, current))
    }

    /** Profile mutation is unavailable until the profile-v3 editor port supplies a typed command. */
    override fun setBoolean(
        key: String,
        field: String,
        value: Boolean,
    ): BackendConfigMutationResult = BackendConfigMutationResult.UNSUPPORTED

    /** Profile mutation is unavailable until the profile-v3 editor port supplies a typed command. */
    override fun selectValue(
        key: String,
        field: String,
        value: String?,
    ): BackendConfigMutationResult = BackendConfigMutationResult.UNSUPPORTED

    /** Document import is unavailable until a dedicated data repository commits to profile-v3. */
    override fun importDocument(
        key: String,
        field: String,
        uri: String,
    ): BackendConfigMutationResult = BackendConfigMutationResult.UNSUPPORTED

    /** Builds the complete current neutral state map. */
    private fun buildStates(): Map<String, BackendConfiguration> = entriesByKey.values.associate { spec ->
        val current = availability(spec.key) ?: missingAvailability()
        spec.key to presentation.project(spec, current)
    }

    /** Fail-closed readiness when a captured key has no provider. */
    private fun missingAvailability(): BackendAvailability = BackendAvailability.NotReady(
        listOf(
            com.ghostlock.app.domain.backend.BackendAvailabilityReason(
                code = "availability-missing",
                message = "Backend readiness is unavailable.",
            ),
        ),
    )
}
