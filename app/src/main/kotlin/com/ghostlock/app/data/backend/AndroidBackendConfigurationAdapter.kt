package com.ghostlock.app.data.backend

import android.content.Context
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfigValue
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationMutation
import com.ghostlock.app.domain.backend.BackendConfigurationPort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Generic Android adapter for configuration capabilities declared by captured [BackendSpec] values.
 *
 * @param context Application context used only to create each typed backend store.
 * @param registryEntries Immutable registry membership captured by the application composition root.
 * @param sessionStore Physical typed-store owner shared with backend selection/session restore.
 */
class AndroidBackendConfigurationAdapter(
    context: Context,
    registryEntries: List<BackendSpec>,
    private val sessionStore: BackendSessionStore,
) : BackendConfigurationPort {
    private val applicationContext = context.applicationContext
    private val entriesByKey = registryEntries.associateBy(BackendSpec::key)
    private val mutableStates = MutableStateFlow(
        entriesByKey.values.mapNotNull { spec ->
            snapshot(spec)?.let { state -> spec.key to state }
        }.toMap(),
    )

    override val states: StateFlow<Map<String, BackendConfiguration>> = mutableStates.asStateFlow()

    /** Rebuilds one snapshot without changing persistence; unknown/failed keys keep prior state. */
    override fun refresh(key: String) {
        val spec = entriesByKey[key] ?: return
        val state = snapshot(spec) ?: return
        mutableStates.value = mutableStates.value + (key to state)
    }

    /** Routes one boolean command through the same generic capability path as every mutation. */
    override fun setBoolean(
        key: String,
        field: String,
        value: Boolean,
    ): BackendConfigMutationResult = mutate(
        key,
        BackendConfigurationMutation.SetBoolean(field, value),
    )

    /** Routes one optional choice through the owning backend capability. */
    override fun selectValue(
        key: String,
        field: String,
        value: String?,
    ): BackendConfigMutationResult = mutate(
        key,
        BackendConfigurationMutation.SelectValue(field, value),
    )

    /** Routes one document URI through the owning backend capability. */
    override fun importDocument(
        key: String,
        field: String,
        uri: String,
    ): BackendConfigMutationResult = mutate(
        key,
        BackendConfigurationMutation.ImportDocument(field, uri),
    )

    /**
     * Resolves a captured backend once and delegates without concrete class/key branching.
     *
     * @return UNSUPPORTED for unknown backend/capability/field; INVALID for thrown validation or
     * persistence failures; APPLIED refreshes exactly once.
     */
    private fun mutate(
        key: String,
        mutation: BackendConfigurationMutation,
    ): BackendConfigMutationResult {
        val spec = entriesByKey[key] ?: return BackendConfigMutationResult.UNSUPPORTED
        val capability = spec.configurationSpec ?: return BackendConfigMutationResult.UNSUPPORTED
        val result = runCatching {
            capability.mutate(configStore(spec), mutation)
        }.getOrDefault(BackendConfigMutationResult.INVALID)
        if (result == BackendConfigMutationResult.APPLIED) {
            refresh(key)
        }
        return result
    }

    /** @return Complete semantic state, or null when projection failed and prior state must remain. */
    private fun snapshot(spec: BackendSpec): BackendConfiguration? = runCatching {
        spec.configurationSpec?.snapshot(configStore(spec)) ?: BackendConfiguration.Static(
            key = spec.key,
            values = spec.rows().map { row ->
                BackendConfigValue(
                    label = applicationContext.getString(row.label),
                    value = row.value,
                )
            },
        )
    }.getOrNull()

    /** @return Existing typed store for one captured backend identity. */
    private fun configStore(spec: BackendSpec) =
        sessionStore.configStore(applicationContext, spec)
}
