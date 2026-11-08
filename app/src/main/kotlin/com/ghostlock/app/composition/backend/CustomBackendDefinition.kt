package com.ghostlock.app.composition.backend

import com.ghostlock.app.backend.backends.lua.CustomBackendRuntime
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.backend.CustomBackendUi

/** Sole Kotlin composition root for the disabled future custom backend package. */
internal object CustomBackendDefinition {
    /** Immutable descriptor, inert runtime, and disabled named Android UI composition. */
    val definition: BackendDefinition = BackendDefinition(
        descriptor = BackendDescriptor(
            key = "CUSTOM",
            selectionPolicy = BackendSelectionPolicy.DISABLED,
            abortPolicy = AbortPolicy.SAFE_DURING_RUN,
            executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
            runCapability = RunCapability.PRODUCT_ROOT,
        ),
        runtime = CustomBackendRuntime,
        ui = CustomBackendUi,
    )
}
