package com.ghostlock.app.ui.backend

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.ui.icon.IconPair

/** Android presentation contract for one built-in backend. */
interface BackendUi {
    /** Localized category or vulnerability label resource. */
    @get:StringRes
    val categoryLabelRes: Int

    /** Localized selector display-name resource. */
    @get:StringRes
    val displayNameRes: Int

    /** Project-owned Material/Miuix icon pair. */
    val icon: IconPair

    /**
     * Renders this backend's named information or settings surface.
     *
     * @param availability Current dynamic readiness supplied by the runtime projection.
     */
    @Composable
    fun Content(availability: BackendAvailability)
}

/**
 * UI-layer projection derived from one outer backend definition.
 *
 * @property descriptor Stable selection and identity facts.
 * @property ui Named Android presentation implementation.
 */
data class BackendUiEntry(
    val descriptor: BackendDescriptor,
    val ui: BackendUi,
)
