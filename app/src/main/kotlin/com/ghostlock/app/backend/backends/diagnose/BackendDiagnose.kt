package com.ghostlock.app.backend.backends.diagnose

import com.ghostlock.app.backend.declarations.BackendPlugin
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendConfigurationSpec
import com.ghostlock.app.domain.backend.BackendParameterRequirements
import com.ghostlock.app.domain.backend.BackendPresentationFact
import com.ghostlock.app.domain.backend.BackendPresentationIdentity
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability

/** Release-visible diagnostic plugin that validates the real Flow/JNA/native/shell transport chain. */
internal object BackendDiagnose {
    /** Immutable non-root plugin available in both debug and release registry membership. */
    val plugin: BackendPlugin = BackendPlugin(
        spec = BackendSpec(
            key = "DIAGNOSE",
            presentation = BackendPresentationIdentity(
                categoryLabel = "Diagnostic",
                displayName = "Diagnose",
                facts = listOf(
                    BackendPresentationFact("native", "JNA shared-library round trip"),
                    BackendPresentationFact("handoff", "host-published shell script"),
                ),
            ),
            selectionPolicy = BackendSelectionPolicy.SELECTABLE,
            abortPolicy = AbortPolicy.BEFORE_START_ONLY,
            executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
            configuration = BackendConfigurationSpec(),
            parameterRequirements = BackendParameterRequirements.EMPTY,
            runCapability = RunCapability.DIAGNOSTIC_ONLY,
        ),
        readinessProbe = { BackendAvailability.Ready() },
        flowFactory = { DiagnoseFlow() },
    )
}
