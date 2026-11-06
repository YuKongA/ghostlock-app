package com.ghostlock.app.backend.backends.lua

import com.ghostlock.app.backend.declarations.BackendPlugin
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendConfigField
import com.ghostlock.app.domain.backend.BackendConfigSchema
import com.ghostlock.app.domain.backend.BackendConfigValueKind
import com.ghostlock.app.domain.backend.BackendConfigurationSpec
import com.ghostlock.app.domain.backend.BackendParameterRequirements
import com.ghostlock.app.domain.backend.BackendPresentationFact
import com.ghostlock.app.domain.backend.BackendPresentationIdentity
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability

/** Disabled restricted Lua operation-composition plugin. */
internal object BackendCustom {
    /** Immutable plugin; it remains NotReady until a real approved operation catalog exists. */
    val plugin: BackendPlugin = BackendPlugin(
        spec = BackendSpec(
            key = "CUSTOM",
            presentation = BackendPresentationIdentity(
                categoryLabel = "Custom",
                displayName = "Lua operation flow",
                facts = listOf(
                    BackendPresentationFact("runtime", "typed operation catalog"),
                    BackendPresentationFact("status", "no approved operation"),
                ),
            ),
            selectionPolicy = BackendSelectionPolicy.DISABLED,
            abortPolicy = AbortPolicy.SAFE_DURING_RUN,
            executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
            configuration = BackendConfigurationSpec(
                schema = BackendConfigSchema(
                    fields = listOf(
                        BackendConfigField(
                            name = "script_uri",
                            kind = BackendConfigValueKind.STRING,
                            required = false,
                            description = "Selected Lua frontend document URI.",
                        ),
                    ),
                ),
                documentMimeTypes = listOf(
                    "text/x-lua",
                    "text/plain",
                    "application/octet-stream",
                ),
            ),
            parameterRequirements = BackendParameterRequirements.EMPTY,
            runCapability = RunCapability.PRODUCT_ROOT,
        ),
        readinessProbe = {
            BackendAvailability.NotReady(
                listOf(
                    BackendAvailabilityReason(
                        code = "lua-operation-catalog-empty",
                        message = "No approved native or executable operation is available to Lua.",
                    ),
                ),
            )
        },
        flowFactory = {
            FlowLuaImpl(
                luaInput = LuaFlowInput(scriptArtifactId = "", calls = emptyList()),
                catalog = LuaOperationCatalog.EMPTY,
            )
        },
    )
}
