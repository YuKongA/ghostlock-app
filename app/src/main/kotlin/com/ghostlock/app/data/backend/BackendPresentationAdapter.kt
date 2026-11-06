package com.ghostlock.app.data.backend

import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendConfigValue
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendSpec

/**
 * Maps a neutral backend declaration into the existing renderer-neutral configuration surface.
 */
class BackendPresentationAdapter {
    /**
     * Captures static facts plus the current typed readiness message.
     *
     * @param spec Neutral immutable spec.
     * @param availability Resource-free dynamic readiness fact.
     * @return Static configuration card without backend callbacks.
     */
    fun project(
        spec: BackendSpec,
        availability: BackendAvailability,
    ): BackendConfiguration.Static = BackendConfiguration.Static(
        key = spec.key,
        values = buildList {
            spec.presentation.facts.forEach { fact ->
                add(BackendConfigValue(fact.label, fact.value))
            }
            availability.reasons.forEach { reason ->
                add(BackendConfigValue("status", reason.message))
            }
        },
    )
}
