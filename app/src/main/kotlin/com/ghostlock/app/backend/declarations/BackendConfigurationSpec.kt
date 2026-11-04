package com.ghostlock.app.backend.declarations

import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationMutation

/**
 * Optional interactive-configuration capability owned by one [BackendSpec].
 *
 * The outer data adapter supplies the typed physical store. Implementations project complete
 * renderer-neutral semantics and apply only fields owned by the same backend object.
 */
interface BackendConfigurationSpec {
    /**
     * Projects the current backend state into one complete semantic UI snapshot.
     *
     * @param store Typed store and Android context for this backend identity.
     * @return Renderer-neutral state; no UI code may parse backend reference encodings.
     */
    fun snapshot(store: BackendConfigStore): BackendConfiguration

    /**
     * Applies one validated mutation through this backend's canonical state authority.
     *
     * @param store Typed store and Android context for this backend identity.
     * @param mutation Neutral mutation selected by stable field name.
     * @return APPLIED, INVALID, or UNSUPPORTED without a fallback backend path.
     */
    fun mutate(
        store: BackendConfigStore,
        mutation: BackendConfigurationMutation,
    ): BackendConfigMutationResult
}
