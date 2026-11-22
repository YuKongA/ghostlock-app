package com.ghostlock.app.ui.backend

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.ui.icon.IconPair

/**
 * Host-owned Android presentation selected only for an existing authenticated catalog member.
 * Plugin-defined types never cross this interface.
 */
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
     * @param availability Resource-free Host catalog/compatibility projection; rendering must not
     * load, instantiate, execute, or probe plugin code.
     */
    @Composable
    fun Content(availability: BackendAvailability)
}

/**
 * Pairs a descriptor from one READY catalog view with a Host-owned presentation adapter.
 * This entry cannot create backend membership or a plugin runtime.
 *
 * @property descriptor Stable selection and identity facts.
 * @property ui Named Android presentation implementation.
 */
data class BackendUiEntry(
    val descriptor: BackendDescriptor,
    val ui: BackendUi,
)
