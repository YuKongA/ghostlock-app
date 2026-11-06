package com.ghostlock.app.domain.finish

/** Stable Finish item kinds supported by the root-script protocol. */
enum class FinishKind(val documentId: String) {
    Command("command"),
    Script("script"),
    Reboot("reboot"),
    ;

    companion object {
        /** @return Exact matching kind, or null for unsupported document input. */
        fun fromDocumentId(documentId: String): FinishKind? =
            entries.firstOrNull { it.documentId == documentId }
    }
}

/** Stable impact of one Finish failure on the product terminal. */
enum class FinishSeverity(val documentId: String) {
    Normal("normal"),
    SafetyCritical("safety_critical"),
    ;

    companion object {
        /** @return Exact matching severity, or null for unsupported document input. */
        fun fromDocumentId(documentId: String): FinishSeverity? =
            entries.firstOrNull { it.documentId == documentId }
    }
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
data class FinishDefinition(
    val items: List<FinishItemDefinition> = emptyList(),
)
