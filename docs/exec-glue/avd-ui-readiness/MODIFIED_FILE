package com.ghostlock.app.backend.declarations.cve43499glued

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.ghostlock.app.R
import com.ghostlock.app.backend.cve43499glued.Backend43499GluedProfileCard
import com.ghostlock.app.backend.cve43499glued.Glued43499ProfileCodec
import com.ghostlock.app.backend.cve43499glued.Resolved43499Profile
import com.ghostlock.app.backend.cve43499glued.flow43499GluedRunner
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.ui.backend.BackendConfigKey
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendConfigType
import com.ghostlock.app.ui.backend.BackendFlowRunner
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.ReadinessReason

/** Selectable 43499 backend backed by the untouched upstream executable. */
object Backend43499Glued : BackendSpec {
    private const val PROFILE_REFERENCE = "profile_reference"

    override val key: String = "43499-Glued"
    override val cve: String = "CVE-2026-43499-Glued"
    override val titleRes: Int = R.string.backend_cve_2026_43499
    override val displayNameRes: Int = R.string.backend_cve_2026_43499_glued
    override val selectable: Boolean = true
    override val runner: BackendFlowRunner = flow43499GluedRunner()
    override val configSchema: BackendConfigSchema = BackendConfigSchema(
        listOf(BackendConfigKey(PROFILE_REFERENCE, BackendConfigType.STRING, "")),
    )
    override val customConfig: (@Composable (Boolean) -> Unit) = { miuix ->
        Backend43499GluedProfileCard(miuix)
    }

    /** Persisted built-in reference or SAF URI; blank means the card still requires a profile choice. */
    var profileReference: String by mutableStateOf("")
        private set
    private var profileValidated: Boolean by mutableStateOf(false)

    /** Short backend facts shown in the card beneath the profile selector. */
    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_channel, "exec (upstream CLI)"),
        BackendRow(R.string.backend_row_payload, "lib43499-Glued.so"),
    )

    /** The selected config is a real run prerequisite; no profile means fail-closed. */
    override fun readiness(): BackendReadiness = if (profileReference.isNotBlank() && profileValidated) {
        BackendReadiness.Ready()
    } else if (profileReference.isNotBlank()) {
        BackendReadiness.NotReady(listOf(ReadinessReason(R.string.backend_43499_profile_invalid)))
    } else {
        BackendReadiness.NotReady(listOf(ReadinessReason(R.string.backend_43499_profile_required)))
    }

    /**
     * Provides the readiness reason already declared by this backend to the home status card.
     *
     * @return Ready for a valid profile, otherwise the first declared profile requirement.
     * @note Readiness remains the sole source of prerequisite state.
     */
    override fun statusTextRes(): Int = when (val current = readiness()) {
        is BackendReadiness.Ready -> R.string.backend_readiness_ready
        is BackendReadiness.NotReady -> current.reasons.firstOrNull()?.messageRes
            ?: R.string.backend_readiness_undeclared
    }

    /** Loads the saved profile and validates it using the same codec as the exec input path. */
    override fun restoreConfig(store: BackendConfigStore) {
        val saved = store.getString(PROFILE_REFERENCE)
        profileReference = saved
        profileValidated = saved.isNotBlank() &&
            runCatching { Glued43499ProfileCodec.resolve(store.context, saved) }.isSuccess
    }

    /** Writes only the declared profile reference; profile content remains in assets or the SAF URI. */
    override fun saveConfig(store: BackendConfigStore) {
        store.putString(PROFILE_REFERENCE, profileReference)
    }

    /** Selects a pre-validated reference and immediately persists it in the declared schema. */
    fun selectProfile(reference: String, store: BackendConfigStore) {
        store.putString(PROFILE_REFERENCE, reference)
        profileReference = reference
        profileValidated = true
    }

    /** Clears the profile choice so readiness and dispatch return to the profile-required state. */
    fun clearProfile(store: BackendConfigStore) {
        store.putString(PROFILE_REFERENCE, "")
        profileReference = ""
        profileValidated = false
    }

    /** Resolves the persisted selection into upstream HOCON text plus current GLK1 bytes. */
    internal fun resolveProfile(context: android.content.Context): Resolved43499Profile =
        Glued43499ProfileCodec.resolve(context, profileReference)
}
