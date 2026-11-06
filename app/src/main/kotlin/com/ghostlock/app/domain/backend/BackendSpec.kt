package com.ghostlock.app.domain.backend

import com.ghostlock.app.domain.run.RunCapability

/** One neutral static fact rendered beneath a backend identity. */
data class BackendPresentationFact(
    val label: String,
    val value: String,
)

/**
 * Renderer-neutral presentation identity.
 *
 * @property categoryLabel Stable category or CVE label.
 * @property displayName Human-readable selector name.
 * @property facts Static explanatory facts; dynamic readiness is supplied separately.
 */
data class BackendPresentationIdentity(
    val categoryLabel: String,
    val displayName: String,
    val facts: List<BackendPresentationFact> = emptyList(),
) {
    init {
        require(categoryLabel.isNotBlank()) { "backend category label is blank" }
        require(displayName.isNotBlank()) { "backend display name is blank" }
        require(facts.size <= MAX_FACTS) { "too many backend presentation facts" }
    }

    private companion object {
        const val MAX_FACTS: Int = 64
    }
}

/**
 * Immutable backend declaration visible to domain and presentation code.
 *
 * @property key Stable registry and wire identity.
 * @property presentation Neutral identity and static facts.
 * @property selectionPolicy Product selection exposure.
 * @property abortPolicy Safe-cancellation policy.
 * @property executionDomain Required host identity/domain.
 * @property configuration Neutral GLK profile configuration contract.
 * @property parameterRequirements Kernel/profile/tuning parameter declarations.
 * @property runCapability Mutually exclusive product or diagnostic capability.
 * @note Flow factory and dynamic readiness are internal plugin responsibilities and are absent here.
 */
data class BackendSpec(
    val key: String,
    val presentation: BackendPresentationIdentity,
    val selectionPolicy: BackendSelectionPolicy,
    val abortPolicy: AbortPolicy,
    val executionDomain: ExecutionDomain,
    val configuration: BackendConfigurationSpec,
    val parameterRequirements: BackendParameterRequirements,
    val runCapability: RunCapability,
) {
    init {
        require(key.matches(KEY_PATTERN)) { "invalid backend key: $key" }
        require(runCapability != RunCapability.UNRESOLVED) {
            "registry backend capability must be resolved: $key"
        }
    }

    /** True when product presentation may offer this backend for selection. */
    val selectable: Boolean
        get() = selectionPolicy == BackendSelectionPolicy.SELECTABLE

    private companion object {
        val KEY_PATTERN: Regex = Regex("[A-Za-z0-9][A-Za-z0-9._:-]{0,95}")
    }
}
