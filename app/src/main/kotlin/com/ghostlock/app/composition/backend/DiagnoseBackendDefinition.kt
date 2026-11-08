package com.ghostlock.app.composition.backend

import com.ghostlock.app.backend.backends.diagnose.DiagnoseBackendRuntime
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.backend.DiagnoseBackendUi

/** Sole Kotlin composition root for the built-in Diagnose backend. */
internal object DiagnoseBackendDefinition {
    /** Immutable descriptor, runtime, and named Android UI composition. */
    val definition: BackendDefinition = BackendDefinition(
        descriptor = BackendDescriptor(
            key = "DIAGNOSE",
            selectionPolicy = BackendSelectionPolicy.SELECTABLE,
            abortPolicy = AbortPolicy.BEFORE_START_ONLY,
            executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
            runCapability = RunCapability.DIAGNOSTIC_ONLY,
        ),
        runtime = DiagnoseBackendRuntime,
        ui = DiagnoseBackendUi,
    )
}
