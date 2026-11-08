package com.ghostlock.app.domain.finish

import kotlinx.serialization.Serializable

/** Stable Finish item kinds supported by the root-script protocol. */
@Serializable
enum class FinishKind {
    Command,
    Script,
    Reboot,
}

/** Stable impact of one Finish failure on the product terminal. */
@Serializable
enum class FinishSeverity {
    Normal,
    SafetyCritical,
}

/**
 * One ordered post-Payload action.
 *
 * @property id Stable result-protocol identity.
 * @property kind Finish behavior.
 * @property severity Product impact when this action fails.
 * @property alwaysRun Whether the action still runs after a required Payload failure.
 * @property artifactUri Source URI for script actions; null for command and reboot.
 * @property args Direct argv tokens or script positional arguments.
 */
@Serializable
data class FinishItemDefinition(
    val id: String,
    val kind: FinishKind,
    val severity: FinishSeverity,
    val alwaysRun: Boolean,
    val artifactUri: String? = null,
    val args: List<String> = emptyList(),
)

/**
 * Immutable, backend-neutral Finish authority.
 *
 * @property items Ordered best-effort actions executed after Payload.
 */
@Serializable
data class FinishDefinition(
    val items: List<FinishItemDefinition> = emptyList(),
)
