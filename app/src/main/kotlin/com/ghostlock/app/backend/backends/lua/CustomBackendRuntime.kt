package com.ghostlock.app.backend.backends.lua

import com.ghostlock.app.backend.declarations.BackendRuntime
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason

/** Disabled custom-package runtime until an approved Lua operation catalog exists. */
internal object CustomBackendRuntime : BackendRuntime {
    /** Returns the explicit disabled reason without probing files or constructing runtime state. */
    override fun availability(): BackendAvailability = BackendAvailability.NotReady(
        listOf(
            BackendAvailabilityReason(
                code = "lua-operation-catalog-empty",
                message = "No approved native or executable operation is available to Lua.",
            ),
        ),
    )

    /** Creates the inert typed Lua Flow retained for the future package boundary. */
    override fun createFlow(): Flow = FlowLuaImpl(
        luaInput = LuaFlowInput(scriptArtifactId = "", calls = emptyList()),
        catalog = LuaOperationCatalog.EMPTY,
    )
}
