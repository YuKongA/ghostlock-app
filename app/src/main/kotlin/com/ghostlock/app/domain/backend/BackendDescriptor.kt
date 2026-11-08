package com.ghostlock.app.domain.backend

import com.ghostlock.app.domain.run.RunCapability

/**
 * Minimal immutable domain description of one same-APK backend.
 *
 * @property key Stable registry, selection, profile, and wire identity.
 * @property selectionPolicy Product selection exposure.
 * @property abortPolicy Safe-cancellation policy.
 * @property executionDomain Required execution process and privilege domain.
 * @property runCapability Product-root or diagnostic-only behavior.
 * @note Presentation, configuration, Flow factories, JNA objects, and mutable state belong to other owners.
 */
data class BackendDescriptor(
    val key: String,
    val selectionPolicy: BackendSelectionPolicy,
    val abortPolicy: AbortPolicy,
    val executionDomain: ExecutionDomain,
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
