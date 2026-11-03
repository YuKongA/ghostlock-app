package com.ghostlock.app.backend.backends.cve43499glued

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.core.flow.FlowSymbol
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.ui.backend.BackendConfigKey
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendConfigStore
import com.ghostlock.app.ui.backend.BackendConfigType
import com.ghostlock.app.ui.backend.BackendFlowParams
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.ReadinessReason

/** Selectable 43499 backend backed by the untouched upstream executable. */
object BackendCve43499Glued : BackendSpec {
    private const val PROFILE_REFERENCE = "profile_reference"

    override val key: String = "43499-Glued"
    override val cve: String = "CVE-2026-43499-Glued"
    override val titleRes: Int = R.string.backend_cve_2026_43499
    override val displayNameRes: Int = R.string.backend_cve_2026_43499_glued
    override val selectable: Boolean = true

    /** B4a：context-free 反射入口（host 进程按此符号执行）。 */
    override val flowSymbol: FlowSymbol = FlowSymbol(
        "com.ghostlock.app.backend.backends.cve43499glued.Flow43499GluedEntry",
        "entry",
    )
    override val configSchema: BackendConfigSchema = BackendConfigSchema(
        listOf(BackendConfigKey(PROFILE_REFERENCE, BackendConfigType.STRING, "")),
    )
    /** 43499-Glued 自定义卡（profile 选择）；唯一 UI 入口。 */
    @Composable
    override fun ConfigCard(miuix: Boolean) {
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

    /** B4a：UI 进程把 profile 解析成 Flow 参数（失败具名 Refused；host 不读 UI 全局）。 */
    override fun flowParams(context: Context): BackendFlowParams =
        Glued43499ExecParams.build(context)

    /** Resolves the persisted selection into upstream HOCON text plus current GLK1 bytes. */
    internal fun resolveProfile(context: Context): Resolved43499Profile =
        Glued43499ProfileCodec.resolve(context, profileReference)
}
