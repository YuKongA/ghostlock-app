package com.ghostlock.app.backend.backends.debugprobe

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.declarations.BackendPlugin
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendConfigurationSpec
import com.ghostlock.app.domain.backend.BackendParameterRequirements
import com.ghostlock.app.domain.backend.BackendPresentationFact
import com.ghostlock.app.domain.backend.BackendPresentationIdentity
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability

/** Debug-only diagnostic plugin declaration with no product-root capability. */
internal object BackendDebugProbe {
    /** Immutable plugin installed only in debug registry membership. */
    val plugin: BackendPlugin = BackendPlugin(
        spec = BackendSpec(
            key = "DEBUG-PROBE",
            presentation = BackendPresentationIdentity(
                categoryLabel = "Diagnostic",
                displayName = "Debug Probe",
                facts = listOf(
                    BackendPresentationFact("channel", "typed RunHost"),
                    BackendPresentationFact("effect", "read-only process snapshot"),
                ),
            ),
            selectionPolicy = if (BuildConfig.DEBUG) {
                BackendSelectionPolicy.SELECTABLE
            } else {
                BackendSelectionPolicy.HIDDEN
            },
            abortPolicy = AbortPolicy.SAFE_DURING_RUN,
            executionDomain = ExecutionDomain.APP_OR_SHIZUKU,
            configuration = BackendConfigurationSpec(),
            parameterRequirements = BackendParameterRequirements.EMPTY,
            runCapability = RunCapability.DIAGNOSTIC_ONLY,
        ),
        readinessProbe = {
            if (BuildConfig.DEBUG) {
                BackendAvailability.Ready()
            } else {
                BackendAvailability.NotReady(
                    listOf(
                        BackendAvailabilityReason(
                            code = "debug-build-required",
                            message = "Debug Probe is available only in debug builds.",
                        ),
                    ),
                )
            }
        },
        flowFactory = { DebugProbeFlow() },
    )
}
