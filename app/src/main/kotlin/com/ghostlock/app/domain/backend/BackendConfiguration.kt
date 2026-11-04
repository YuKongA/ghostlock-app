package com.ghostlock.app.domain.backend

import kotlinx.coroutines.flow.StateFlow

/** One renderer-neutral static backend fact. */
data class BackendConfigValue(
    val label: String,
    val value: String,
)

/** Renderer-neutral leading icon understood by the shared UI kit. */
enum class BackendConfigIcon {
    NONE,
    SECURITY,
}

/** Semantic role of one choice; the UI never parses backend-owned reference encodings. */
enum class BackendChoiceKind {
    NONE,
    BUILT_IN,
    IMPORTED,
}

/** One fully rendered choice paired with the value sent back to the owning backend. */
data class BackendChoiceOption(
    val value: String?,
    val label: String,
    val kind: BackendChoiceKind,
)

/**
 * Renderer-neutral configuration state selected by stable backend key.
 *
 * The subtype is the semantic component kind; Material and Miuix renderers choose only appearance.
 */
sealed interface BackendConfiguration {
    val key: String

    /** Read-only key/value facts for a backend without interactive configuration. */
    data class Static(
        override val key: String,
        val values: List<BackendConfigValue>,
    ) : BackendConfiguration

    /** One boolean backend option. */
    data class Toggle(
        override val key: String,
        val field: String,
        val title: String,
        val enabled: Boolean,
        val enabledSummary: String,
        val disabledSummary: String,
        val icon: BackendConfigIcon,
    ) : BackendConfiguration

    /** Built-in choice plus optional imported document. */
    data class ChoiceDocument(
        override val key: String,
        val field: String,
        val title: String,
        val summary: String,
        val options: List<BackendChoiceOption>,
        val selectedIndex: Int,
        val importTitle: String,
        val importSummary: String,
        val mimeTypes: List<String>,
        val errorText: String,
        val invalid: Boolean,
    ) : BackendConfiguration

    /** One imported document with fully rendered facts and an optional bounded detail. */
    data class Document(
        override val key: String,
        val field: String,
        val importTitle: String,
        val documentSummary: String,
        val mimeTypes: List<String>,
        val facts: List<BackendConfigValue>,
        val details: String?,
    ) : BackendConfiguration
}

/** One renderer-neutral mutation routed to the backend that owns [field]. */
sealed interface BackendConfigurationMutation {
    val field: String

    /** Writes a boolean field. */
    data class SetBoolean(
        override val field: String,
        val value: Boolean,
    ) : BackendConfigurationMutation

    /** Selects a backend-owned value; null clears the selection. */
    data class SelectValue(
        override val field: String,
        val value: String?,
    ) : BackendConfigurationMutation

    /** Imports one document URI string through the owning backend. */
    data class ImportDocument(
        override val field: String,
        val uri: String,
    ) : BackendConfigurationMutation
}

/** Explicit result of one backend-configuration mutation. */
enum class BackendConfigMutationResult {
    APPLIED,
    INVALID,
    UNSUPPORTED,
}

/** Domain boundary for backend configuration state and validated mutation commands. */
interface BackendConfigurationPort {
    /** Configuration snapshots for the captured registry, indexed by stable backend key. */
    val states: StateFlow<Map<String, BackendConfiguration>>

    /** Refreshes one captured backend snapshot without changing persisted state. */
    fun refresh(key: String)

    /** Writes one declared boolean field. */
    fun setBoolean(key: String, field: String, value: Boolean): BackendConfigMutationResult

    /** Selects or clears one declared built-in string option. */
    fun selectValue(key: String, field: String, value: String?): BackendConfigMutationResult

    /** Validates and persists a selected document URI string. */
    fun importDocument(key: String, field: String, uri: String): BackendConfigMutationResult
}
