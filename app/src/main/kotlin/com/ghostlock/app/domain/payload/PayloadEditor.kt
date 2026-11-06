package com.ghostlock.app.domain.payload

/**
 * Environment projection for one supported root-manager handoff target.
 *
 * @property name Stable manager name.
 * @property installedPackage First installed candidate package, or null.
 * @property iconPackage Package used for the editor icon or placeholder identity.
 * @property launchOnly Whether this manager omits the late-load safe-mode path.
 */
data class PayloadRootManagerOption(
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
 * Editable projection of [PayloadDefinition] plus non-persisted environment facts.
 *
 * @property rootManager Selected manager name.
 * @property safeMode Selected manager safe-mode argument.
 * @property payloads Ordered editable items.
 * @property rootManagers Current package-installation facts.
 */
data class PayloadEditorState(
    val rootManager: String,
    val safeMode: Boolean,
    val payloads: List<PayloadSlot>,
    val rootManagers: List<PayloadRootManagerOption>,
) {
    /** @return True when an enabled item lacks required input. */
    val hasIncompletePayload: Boolean
        get() = payloads.any { it.enabled && !it.isComplete() }

    /** @return Persistable definition without environment facts. */
    fun toDefinition(): PayloadDefinition = PayloadDefinition(
        rootManager = rootManager,
        safeMode = safeMode,
        items = payloads,
    )
}
