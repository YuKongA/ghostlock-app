package com.ghostlock.app.domain.settings

import com.ghostlock.app.domain.privilege.AttackExecutorKind
import kotlinx.serialization.Serializable

/**
 * Persisted application-wide selections that are independent of live platform facts.
 *
 * @property selectedExecutor Process family requested for backend execution.
 * @property backendSelectionState Distinguishes a new install from an explicit or invalidated choice.
 * @property selectedBackendKey Stable backend key only while [backendSelectionState] is [BackendSelectionState.SELECTED].
 * @note Binder permission, catalog membership, availability and uid are deliberately absent.
 */
@Serializable
data class AppSelection(
    val selectedExecutor: AttackExecutorKind = AttackExecutorKind.APP,
    val backendSelectionState: BackendSelectionState = BackendSelectionState.FRESH_UNINITIALIZED,
    val selectedBackendKey: String? = null,
) {
    init {
        when (backendSelectionState) {
            BackendSelectionState.SELECTED -> requireValidBackendKey(
                requireNotNull(selectedBackendKey) { "SELECTED requires a backend key" },
            )
            BackendSelectionState.FRESH_UNINITIALIZED,
            BackendSelectionState.NEEDS_USER_CHOICE,
            -> require(selectedBackendKey == null) {
                "$backendSelectionState must not retain a backend key"
            }
        }
    }
}

/** Durable interpretation of [AppSelection.selectedBackendKey]; null alone is intentionally ambiguous. */
@Serializable
enum class BackendSelectionState {
    FRESH_UNINITIALIZED,
    SELECTED,
    NEEDS_USER_CHOICE,
}

private fun requireValidBackendKey(key: String) {
    require(key.matches(Regex("[A-Za-z0-9][A-Za-z0-9._:-]{0,95}"))) { "invalid backend key" }
}
