package com.ghostlock.app.domain.payload

import java.util.UUID

/** Stable Payload kinds persisted by the version-three Payload document. */
enum class PayloadKind(val documentId: String) {
    RootManager("root_manager"),
    Command("command"),
    Lkm("lkm"),
    ;

    companion object {
        /**
         * Resolves a document identifier without accepting aliases or legacy spellings.
         *
         * @param documentId Canonical version-three identifier.
         * @return Matching kind, or null when the document is unsupported.
         */
        fun fromDocumentId(documentId: String): PayloadKind? =
            entries.firstOrNull { it.documentId == documentId }
    }
}

/** Stable success evidence required by one Payload item. */
enum class PayloadAcceptance(val documentId: String) {
    ExitZero("exit_zero"),
    DaemonAck("daemon_ack"),
    ModulePresent("module_present"),
    ;

    companion object {
        /**
         * Resolves an exact version-three acceptance identifier.
         *
         * @param documentId Canonical identifier.
         * @return Matching acceptance, or null for unsupported input.
         */
        fun fromDocumentId(documentId: String): PayloadAcceptance? =
            entries.firstOrNull { it.documentId == documentId }
    }
}

/**
 * One ordered user target in a [PayloadDefinition].
 *
 * @property id Stable result-protocol identity.
 * @property kind Payload behavior.
 * @property enabled Whether the item participates in a frozen run.
 * @property required Whether a non-success terminal state triggers fail-fast.
 * @property acceptance Required success evidence; it is never inferred from output text.
 * @property scriptMode Command mode: zero means direct argv text and one means a staged script URI.
 * @property command Whitespace-delimited direct argv text; shell expansion is not supported.
 * @property scriptUri Source document URI for script mode.
 * @property lkmUri Source document URI for an LKM.
 * @property lkmParams Whitespace-delimited module arguments.
 */
data class PayloadSlot(
    val id: String,
    val kind: PayloadKind,
    val enabled: Boolean = true,
    val required: Boolean = true,
    val acceptance: PayloadAcceptance = kind.defaultAcceptance(),
    val scriptMode: Int = 0,
    val command: String = "",
    val scriptUri: String? = null,
    val lkmUri: String? = null,
    val lkmParams: String = "",
) {
    /** @return True when the enabled item has every input required by its kind. */
    fun isComplete(): Boolean = when (kind) {
        PayloadKind.RootManager -> true
        PayloadKind.Command -> if (scriptMode == 0) command.isNotBlank() else !scriptUri.isNullOrBlank()
        PayloadKind.Lkm -> !lkmUri.isNullOrBlank()
    }
}

/** @return The only acceptance compatible with the current Payload kind. */
fun PayloadKind.defaultAcceptance(): PayloadAcceptance = when (this) {
    PayloadKind.RootManager -> PayloadAcceptance.DaemonAck
    PayloadKind.Command -> PayloadAcceptance.ExitZero
    PayloadKind.Lkm -> PayloadAcceptance.ModulePresent
}

/**
 * Immutable, backend-neutral Payload authority frozen for one run.
 *
 * @property rootManager Selected manager implementation for [PayloadKind.RootManager].
 * @property safeMode Whether a compatible manager receives its safe-mode argument.
 * @property items Ordered item definitions; disabled rows remain editable but are not frozen into a run.
 */
data class PayloadDefinition(
    val rootManager: String = "KernelSU",
    val safeMode: Boolean = false,
    val items: List<PayloadSlot> = listOf(
        PayloadSlot(DEFAULT_ROOT_PAYLOAD_ID, PayloadKind.RootManager),
    ),
) {
    /** @return Enabled items in exact user order. */
    fun enabledItems(): List<PayloadSlot> = items.filter { it.enabled }
}

/** Default root-manager Payload item identity. */
const val DEFAULT_ROOT_PAYLOAD_ID: String = "payload-root"

/** @return A session-unique editable Payload item of the requested kind. */
fun newPayloadSlot(kind: PayloadKind): PayloadSlot = PayloadSlot(
    id = "payload-" + UUID.randomUUID().toString().replace("-", ""),
    kind = kind,
)

/**
 * Static root-manager launch definition used by both editor environment projection and rendering.
 *
 * @property name Stable persisted selection.
 * @property packages Candidate Android packages in preference order.
 * @property binary Native manager entrypoint name.
 * @property args Direct argv passed to the entrypoint.
 * @property launchOnly Whether the manager is acknowledged through its existing daemon instead of late-load.
 */
data class PayloadRootManager(
    val name: String,
    val packages: List<String>,
    val binary: String,
    val args: List<String>,
    val launchOnly: Boolean = false,
)

/** Single catalog for supported root-manager Payload semantics. */
object PayloadRootManagers {
    /** Ordered manager choices shown by the editor and consumed by the renderer. */
    val all: List<PayloadRootManager> = listOf(
        PayloadRootManager(
            name = "KernelSU",
            packages = listOf("me.weishu.kernelsu"),
            binary = "libksud.so",
            args = listOf("late-load", "--allow-shell"),
        ),
        PayloadRootManager(
            name = "KernelSU Next",
            packages = listOf("me.weishu.kernelsu.pr"),
            binary = "libksud.so",
            args = listOf("late-load", "--allow-shell"),
        ),
        PayloadRootManager(
            name = "ReSukiSU",
            packages = listOf("com.resukisu.resukisu"),
            binary = "libksud.so",
            args = listOf("late-load", "--allow-shell"),
        ),
        PayloadRootManager(
            name = "SuperManager",
            packages = listOf("com.kowx712.supermanager"),
            binary = "libksud.so",
            args = listOf("late-load", "--allow-shell"),
        ),
        PayloadRootManager(
            name = "FolkPatch",
            packages = listOf("me.yuki.folk", "me.bmax.apatch"),
            binary = "",
            args = emptyList(),
            launchOnly = true,
        ),
    )

    /**
     * Looks up an exact manager name.
     *
     * @param name Persisted manager name.
     * @return Definition, or null when the document names an unsupported manager.
     */
    fun find(name: String): PayloadRootManager? = all.firstOrNull { it.name == name }
}
