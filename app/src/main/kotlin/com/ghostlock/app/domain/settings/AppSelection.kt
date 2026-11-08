package com.ghostlock.app.domain.settings

import com.ghostlock.app.domain.privilege.AttackExecutorKind
import kotlinx.serialization.Serializable

/**
 * Persisted application-wide selections that are independent of live platform facts.
 *
 * @property selectedExecutor Process family requested for backend execution.
 * @property selectedBackendKey Stable backend key, or null before a selectable backend is resolved.
 * @note Binder permission, registry membership, availability and uid are deliberately absent.
 */
@Serializable
data class AppSelection(
    val selectedExecutor: AttackExecutorKind = AttackExecutorKind.APP,
    val selectedBackendKey: String? = null,
)
