package com.ghostlock.app.domain.finish

import java.util.UUID

/** Stable payload kinds exposed to the finish editor. */
enum class FinishPayloadKind {
    Root,
    Command,
    Lkm,
}

/**
 * Renderer- and storage-neutral editable payload slot.
 *
 * @property id Stable list identity retained across edits and reordering.
 * @property kind Payload behavior selected for this slot.
 * @property enabled Whether the slot participates in the generated finish request.
 * @property scriptMode Command mode: zero for inline command and one for a document URI.
 * @property command Inline command body.
 * @property scriptUri Selected command-script document URI.
 * @property lkmUri Selected kernel-module document URI.
 * @property lkmParams Optional kernel-module arguments.
 */
data class FinishPayloadSlot(
    val id: String,
    val kind: FinishPayloadKind,
    val enabled: Boolean = true,
    val scriptMode: Int = 0,
    val command: String = "",
    val scriptUri: String? = null,
    val lkmUri: String? = null,
    val lkmParams: String = "",
) {
    /** @return True when this enabled slot has every input required by its kind. */
    fun isComplete(): Boolean = when (kind) {
        FinishPayloadKind.Root -> true
        FinishPayloadKind.Command -> if (scriptMode == 0) command.isNotBlank() else !scriptUri.isNullOrBlank()
        FinishPayloadKind.Lkm -> !lkmUri.isNullOrBlank()
    }
}

/**
 * Environment projection for one supported root-manager handoff target.
 *
 * @property name Stable manager name stored by the existing finish contract.
 * @property installedPackage First installed package matching this manager, or null.
 * @property iconPackage Installed package or the first candidate used for a placeholder icon.
 * @property launchOnly Whether this target omits late-load safe-mode support.
 */
data class FinishRootManagerOption(
    val name: String,
    val installedPackage: String?,
    val iconPackage: String,
    val launchOnly: Boolean,
) {
    /** @return True when at least one declared manager package is installed. */
    val installed: Boolean
        get() = installedPackage != null
}

/**
 * Complete editable finish projection; non-editor handoff fields remain owned by the adapter.
 *
 * @property rootManager Selected root-manager name.
 * @property safeMode Whether the selected late-load manager receives safe mode.
 * @property payloads Ordered editable payload slots.
 * @property rootManagers Current install-state projection for supported manager targets.
 */
data class FinishEditorState(
    val rootManager: String,
    val safeMode: Boolean,
    val payloads: List<FinishPayloadSlot>,
    val rootManagers: List<FinishRootManagerOption>,
) {
    /** @return True when an enabled payload lacks required input. */
    val hasIncompletePayload: Boolean
        get() = payloads.any { it.enabled && !it.isComplete() }
}

/** @return A new session-unique editable payload slot of the requested kind. */
fun newFinishPayloadSlot(kind: FinishPayloadKind): FinishPayloadSlot = FinishPayloadSlot(
    id = "slot-" + UUID.randomUUID(),
    kind = kind,
)
