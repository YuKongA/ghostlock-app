package com.ghostlock.app.data.backend

import android.content.Context
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.domain.backend.BackendRuntimeSpec

/**
 * Maps the existing outer backend plugin contract into a pure domain projection.
 *
 * @param context Application context used only to resolve the backend-owned localized status resource.
 */
class BackendPresentationAdapter(
    private val context: Context,
) {
    /**
     * Captures the current readiness and status declaration of one backend object.
     *
     * @param spec Registered outer plugin contract.
     * @return Renderer-neutral runtime projection with no Android types.
     */
    fun project(spec: BackendSpec): BackendRuntimeSpec {
        return spec.runtimeSpec(context::getString)
    }
}
