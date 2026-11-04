package com.ghostlock.app.backend.backends.cve43499glued

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.core.flow.FlowSymbol
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.backend.declarations.BackendConfigKey
import com.ghostlock.app.backend.declarations.BackendConfigSchema
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendConfigType
import com.ghostlock.app.backend.declarations.BackendConfigurationSpec
import com.ghostlock.app.backend.declarations.BackendFlowParams
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.ReadinessReason
import com.ghostlock.app.domain.backend.BackendChoiceKind
import com.ghostlock.app.domain.backend.BackendChoiceOption
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationMutation
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Profile selection facts owned by the backend rather than a Compose renderer. */
internal data class Backend43499ProfileState(
    val reference: String = "",
    val validated: Boolean = false,
)

/** Selectable 43499 backend backed by the untouched upstream executable. */
object BackendCve43499Glued : BackendSpec {
    private const val PROFILE_REFERENCE = "profile_reference"
    private val profileMimeTypes = listOf("text/plain", "application/octet-stream", "*/*")

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
    private val mutableProfileState = MutableStateFlow(Backend43499ProfileState())

    /** Immutable profile-selection stream consumed by the presentation adapter. */
    internal val profileState: StateFlow<Backend43499ProfileState> =
        mutableProfileState.asStateFlow()

    /** Persisted built-in reference or SAF URI; blank means the card still requires a profile choice. */
    val profileReference: String
        get() = profileState.value.reference

    /** Standard semantic configuration projection and mutation contract for profile selection. */
    override val configurationSpec: BackendConfigurationSpec = object : BackendConfigurationSpec {
        /** @return Complete choice/import semantics without exposing reference parsing to UI. */
        override fun snapshot(store: BackendConfigStore): BackendConfiguration {
            val profile = profileState.value
            val builtIns = runCatching {
                Glued43499ProfileCodec.builtins(store.context).map { it.release }
            }.getOrDefault(emptyList())
            val importedName = profile.reference.takeIf { it.startsWith("uri:") }
                ?.removePrefix("uri:")
                ?.let { raw -> displayName(store.context, Uri.parse(raw)) }
            val importedLabel = importedName?.takeIf(String::isNotBlank)?.let { name ->
                store.context.getString(R.string.backend_43499_imported_profile, name)
            }
            val options = buildList {
                add(
                    BackendChoiceOption(
                        value = null,
                        label = store.context.getString(R.string.backend_43499_profile_none),
                        kind = BackendChoiceKind.NONE,
                    ),
                )
                builtIns.forEach { release ->
                    add(BackendChoiceOption(release, release, BackendChoiceKind.BUILT_IN))
                }
                if (importedLabel != null) {
                    add(
                        BackendChoiceOption(
                            value = profile.reference,
                            label = importedLabel,
                            kind = BackendChoiceKind.IMPORTED,
                        ),
                    )
                }
            }
            val selectedIndex = when {
                profile.reference.startsWith("builtin:") -> {
                    val release = profile.reference.removePrefix("builtin:")
                    options.indexOfFirst { option ->
                        option.kind == BackendChoiceKind.BUILT_IN && option.value == release
                    }.takeIf { it >= 0 } ?: 0
                }

                importedLabel != null -> options.lastIndex
                else -> 0
            }
            val summary = when {
                profile.reference.startsWith("builtin:") ->
                    profile.reference.removePrefix("builtin:")
                importedLabel != null -> importedLabel
                else -> store.context.getString(R.string.backend_43499_profile_required)
            }
            return BackendConfiguration.ChoiceDocument(
                key = key,
                field = PROFILE_REFERENCE,
                title = store.context.getString(R.string.backend_43499_profile_label),
                summary = summary,
                options = options,
                selectedIndex = selectedIndex,
                importTitle = store.context.getString(R.string.backend_43499_import_profile),
                importSummary = store.context.getString(R.string.backend_43499_import_profile_summary),
                mimeTypes = profileMimeTypes,
                errorText = store.context.getString(R.string.backend_43499_profile_invalid),
                invalid = profile.reference.isNotBlank() && !profile.validated,
            )
        }

        /** Applies only the profile-reference choice/import commands owned by this backend. */
        override fun mutate(
            store: BackendConfigStore,
            mutation: BackendConfigurationMutation,
        ): BackendConfigMutationResult {
            if (mutation.field != PROFILE_REFERENCE) {
                return BackendConfigMutationResult.UNSUPPORTED
            }
            when (mutation) {
                is BackendConfigurationMutation.SelectValue -> {
                    val release = mutation.value
                    if (release.isNullOrBlank()) {
                        clearProfile(store)
                    } else {
                        val reference = "builtin:$release"
                        Glued43499ProfileCodec.resolve(store.context, reference)
                        selectProfile(reference, store)
                    }
                }

                is BackendConfigurationMutation.ImportDocument -> {
                    if (mutation.uri.isBlank()) return BackendConfigMutationResult.INVALID
                    val parsed = Uri.parse(mutation.uri)
                    if (parsed.scheme.isNullOrBlank()) return BackendConfigMutationResult.INVALID
                    store.context.contentResolver.takePersistableUriPermission(
                        parsed,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION,
                    )
                    Glued43499ProfileCodec.validateImported(store.context, parsed)
                    selectProfile("uri:$parsed", store)
                }

                is BackendConfigurationMutation.SetBoolean ->
                    return BackendConfigMutationResult.UNSUPPORTED
            }
            return BackendConfigMutationResult.APPLIED
        }
    }

    /** Short backend facts shown in the card beneath the profile selector. */
    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_channel, "exec (upstream CLI)"),
        BackendRow(R.string.backend_row_payload, "lib43499-Glued.so"),
    )

    /** The selected config is a real run prerequisite; no profile means fail-closed. */
    override fun readiness(): BackendReadiness {
        val selection = profileState.value
        return if (selection.reference.isNotBlank() && selection.validated) {
            BackendReadiness.Ready()
        } else if (selection.reference.isNotBlank()) {
            BackendReadiness.NotReady(listOf(ReadinessReason(R.string.backend_43499_profile_invalid)))
        } else {
            BackendReadiness.NotReady(listOf(ReadinessReason(R.string.backend_43499_profile_required)))
        }
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
        mutableProfileState.value = Backend43499ProfileState(
            reference = saved,
            validated = saved.isNotBlank() &&
                runCatching { Glued43499ProfileCodec.resolve(store.context, saved) }.isSuccess,
        )
    }

    /** Writes only the declared profile reference; profile content remains in assets or the SAF URI. */
    override fun saveConfig(store: BackendConfigStore) {
        store.putString(PROFILE_REFERENCE, profileReference)
    }

    /** Selects a pre-validated reference and immediately persists it in the declared schema. */
    fun selectProfile(reference: String, store: BackendConfigStore) {
        store.putString(PROFILE_REFERENCE, reference)
        mutableProfileState.value = Backend43499ProfileState(reference, validated = true)
    }

    /** Clears the profile choice so readiness and dispatch return to the profile-required state. */
    fun clearProfile(store: BackendConfigStore) {
        store.putString(PROFILE_REFERENCE, "")
        mutableProfileState.value = Backend43499ProfileState()
    }

    /** B4a：UI 进程把 profile 解析成 Flow 参数（失败具名 Refused；host 不读 UI 全局）。 */
    override fun flowParams(context: Context): BackendFlowParams =
        Glued43499ExecParams.build(context)

    /** Resolves the persisted selection into upstream HOCON text plus current GLK1 bytes. */
    internal fun resolveProfile(context: Context): Resolved43499Profile =
        Glued43499ProfileCodec.resolve(context, profileReference)

    /** @return Provider display name or a stable URI path fallback. */
    private fun displayName(context: Context, uri: Uri): String =
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        } ?: uri.lastPathSegment ?: "document"
}
