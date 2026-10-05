package com.ghostlock.app.data

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import androidx.core.net.toUri
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.plugin.EnabledPlugin
import com.ghostlock.app.data.plugin.PluginDescriptor
import com.ghostlock.app.data.plugin.PluginEmission
import com.ghostlock.app.data.plugin.PluginOverrides
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.data.profile.CpuPairView
import com.ghostlock.app.data.profile.GHOSTLOCK_PROFILE_SCHEMA_VERSION
import com.ghostlock.app.data.profile.Glkv3Encoder
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import com.ghostlock.app.data.profile.ProfileMerger
import com.ghostlock.app.data.profile.ProfileResolver
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ExecutionFieldValue
import com.ghostlock.app.domain.model.ExecutionMode
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.ProfileFieldNode
import com.ghostlock.app.domain.repository.ProfileConfigController
import java.io.File
import java.nio.charset.StandardCharsets

/**
 * Controller-centered profile architecture. HOCON is the persistence format
 * and the value model (Map/List/scalars) is the in-memory representation,
 * while this class is the single authority for loading it, merging
 * builtin/imported/override layers, persisting sparse edits and producing the
 * document handed to the native process.
 *
 * Imported profiles come verbatim from [userProfiles]; every edit (general,
 * route and advanced) persists as a sparse override in preferences,
 * so stored user documents are never rewritten.
 */
internal class AndroidProfileConfigController(
    context: Context,
    private val filesDir: File,
    private val userProfiles: UserProfileStore,
    private val preferences: SharedPreferences,
    /**
     * App-level backend selection injected into every document this controller
     * builds. Defaults to 43499 so existing callers keep byte-identical output.
     */
    private val backendSelection: () -> BackendKind = { BackendKind.Default },
    /**
     * App-level execution mode injected alongside [backendSelection]. It fixes
     * the sparse triple the document addresses: 43284 (or the UMH mode) emits
     * pagecache_write + umh_forward, while the 43499 modes keep their mapping.
     * Defaults to General so existing callers stay byte-identical.
     */
    private val executionModeSelection: () -> ExecutionMode = { ExecutionMode.General },
    /**
     * S4 R6b combination token selection. When present it is the single
     * selection authority (backend, steps, route and terminal all derive from
     * it); null keeps the legacy mode/backend pair as the selection source for
     * byte-compatible callers.
     */
    private val combinationSelection: (() -> CombinationSpec)? = null,
    /**
     * P1: the ENABLED plugins (registry row + probe descriptor) the repository
     * resolved. The controller applies the user's overrides to them, because the
     * override store is the controller's. Null/empty keeps every existing caller
     * byte-identical.
     */
    private val pluginSelection: (() -> List<EnabledPlugin>)? = null,
    /**
     * Editing sessions pin the imported document instead of consulting the
     * live selection, and keep their overrides in [preferences] (a private
     * session store), so they never touch the attack controller.
     */
    private val forcedUserProfile: String? = null,
    /** Editing sessions also pin the builtin source of the live controller. */
    private val forcedBuiltinRelease: String? = null,
) : ProfileConfigController {
    private val appContext = context.applicationContext
    private val assetLoader = AssetConfigLoader(appContext)
    private val lock = Any()
    private var cachedRelease: String? = null
    private var cachedProfile: Profile? = null

    override suspend fun load(release: String, pair: CpuPair): ProfileConfig {
        val deviceRelease = release
        val advanced = readAdvancedOverride(deviceRelease)
        val full = resolveCurrent(deviceRelease, pair, advanced, includeImported = true)
        if (full == null) {
            cache(deviceRelease, null)
            return ProfileConfig(release = deviceRelease, hasProfile = false)
        }
        val baseline = resolveCurrent(deviceRelease, pair, null, includeImported = false) ?: full
        val route = routeNameOf(full)
        val invalidPaths = validateProfileFields(full, route) +
            validate43284Fields(full) +
            ProfileResolver.validateMerged(full, route).mapTo(mutableSetOf()) { it.fieldPath }
        /* Invalid fields missing from the resolved document still get a row,
         * otherwise the run stays blocked with no red field to fix. */
        materializeInvalidPaths(full, invalidPaths)
        /* The editor shows every field of the active route plus the shared
         * geometry, so a field the profile did not carry appears as an
         * editable `null` row instead of being invisible. */
        val complete = completeProfileFields(full, route)
        complete43284Fields(complete)
        val roots = buildTree(complete, "", baseline, advanced)
        cache(deviceRelease, buildNativeDocument(deviceRelease, full))
        return ProfileConfig(
            release = deviceRelease,
            hasProfile = true,
            roots = roots,
            general = generalFields(full, baseline, route),
            route = route,
            invalidPaths = invalidPaths,
        )
    }

    /**
     * Kotlin-side geometry/credential validation. The profile declares its
     * route explicitly; fields that belong to other routes may stay absent.
     */
    private fun validateProfileFields(
        profile: ValueMap,
        explicitRoute: String?,
    ): Set<String> {
        val invalid = mutableSetOf<String>()
        /* HOCON booleans (compact_waiter / recommend_vr_guard) read as 1/0. */
        fun value(path: String): Long? = when (val raw = profile.getValueAt(path)) {
            is Boolean -> if (raw) 1L else 0L
            else -> profile.getLongAt(path)
        }
        fun requireNonZero(vararg paths: String) {
            paths.forEach { path ->
                val current = value(path)
                if (current == null || current == 0L) invalid += path
            }
        }

        /* The route is profile-controlled: a missing or unknown branch is
         * invalid. Legacy documents get their route from the converter, so an
         * unresolved route here means the profile is genuinely broken. */
        val route = explicitRoute?.takeIf { it in ProfileConfig.Routes }
        if (route == null) invalid += "route"

        requireNonZero(*RouteCommonRequired.toTypedArray())
        val major = value("kernel_major")
        if (major != 5L && major != 6L) invalid += "kernel_major"

        val copySize = value("cred.copy_size")
        if (copySize == null || copySize == 0L) invalid += "cred.copy_size"
        /* Absent cred.usage_offset decodes as 0 and passes, exactly like native. */
        val usageOffset = value("cred.usage_offset") ?: 0L
        if (copySize != null && usageOffset + SizeofU32 > copySize) {
            invalid += "cred.usage_offset"
        }

        val capsCount = value("cred.caps_count")
        val capsOffset = value("cred.caps_offset")
        if (capsCount == null || capsCount == 0L) invalid += "cred.caps_count"
        if (copySize != null && capsCount != null && capsOffset != null &&
            capsOffset + capsCount * SizeofU64 > copySize
        ) {
            invalid += "cred.caps_offset"
            invalid += "cred.caps_count"
        }

        val refCount = value("cred.ref_count") ?: 0L
        if (refCount > 4) invalid += "cred.ref_count"
        for (index in 0 until 4) {
            if (index >= refCount) break
            val image = value("cred.ref${index}_image")
            if (image == null || image == 0L) invalid += "cred.ref${index}_image"
            val offset = value("cred.ref${index}_offset")
            if (copySize != null && offset != null && offset + SizeofU64 > copySize) {
                invalid += "cred.ref${index}_offset"
            }
        }

        val routePrefix = route?.let { "route.$it" } ?: "route"
        /* Vocabulary keys come from the enum, never from literals; an
         * unrecognised spelling still falls through to the else branch. */
        when (RouteKind.resolve(route)) {
            RouteKind.TCP_ZEROCOPY -> {
                val compact = value("$routePrefix.compact_waiter")
                if (compact == null || compact == 0L) {
                    invalid += "$routePrefix.compact_waiter"
                } else if (compact < 0L || compact > 0xffL) {
                    invalid += "$routePrefix.compact_waiter"
                }
            }

            RouteKind.SELECT_STACK -> {
                val shift = value("$routePrefix.waiter_shift")
                if (shift == null ||
                    shift < Int.MIN_VALUE.toLong() || shift > Int.MAX_VALUE.toLong()
                ) {
                    invalid += "$routePrefix.waiter_shift"
                }
            }

            RouteKind.MULTICAST_WAITER -> {
                for (field in RouteMulticastFields) {
                    val current = value("$routePrefix.$field")
                    if (current == null || current == 0L) {
                        invalid += "$routePrefix.$field"
                    } else if (current < 0L || current > UInt.MAX_VALUE.toLong()) {
                        invalid += "$routePrefix.$field"
                    }
                }
                val compact = value("$routePrefix.compact_waiter")
                if (compact == null || compact == 0L) {
                    invalid += "$routePrefix.compact_waiter"
                } else if (compact < 0L || compact > 0xffL) {
                    invalid += "$routePrefix.compact_waiter"
                }
                for ((field, max) in RouteMulticastTuning) {
                    val current = value("$routePrefix.$field")
                    if (current != null && (current < 0L || current > max)) {
                        invalid += "$routePrefix.$field"
                    }
                }
                requireNonZero(
                    "kernelsnitch.mm_struct_sz",
                    "offset.empty_zero_page",
                    "cred.ref_count",
                )
                if (copySize != null && copySize < 0xa0L) invalid += "cred.copy_size"
                val waiterOff = value("$routePrefix.waiter_off")
                if (waiterOff == null || waiterOff <= 0L || waiterOff > Int.MAX_VALUE.toLong()) {
                    invalid += "$routePrefix.waiter_off"
                }
                val bufferSize = value("$routePrefix.buffer_size")
                val lockOffset = value("$routePrefix.lock_offset")
                if (waiterOff != null && waiterOff > 0L && waiterOff <= Int.MAX_VALUE.toLong() &&
                    bufferSize != null && bufferSize in 0L..UInt.MAX_VALUE.toLong() &&
                    lockOffset != null && lockOffset in 0L..UInt.MAX_VALUE.toLong() &&
                    waiterOff + lockOffset + SizeofU64 > bufferSize
                ) {
                    invalid += "$routePrefix.waiter_off"
                    invalid += "$routePrefix.lock_offset"
                    invalid += "$routePrefix.buffer_size"
                }
            }

            /* No route declared, or a spelling the vocabulary does not know:
             * keep the previous behaviour (no route-specific validation). */
            else -> Unit
        }
        return invalid
    }

    /**
     * S4 R4 validation for the 43284 private section. Mirrors the editor's
     * [isFieldInputInvalid] for the resolved document so the run gate blocks an
     * out-of-range handshake knob or a malformed policy path, not just the text
     * field highlight. A field absent from the document is left to the native
     * default and never flagged.
     */
    private fun validate43284Fields(profile: ValueMap): Set<String> {
        if (!is43284Selection()) return emptySet()
        val invalid = mutableSetOf<String>()
        for (path in Cve2026_43284Fields.StringPaths) {
            val text = profile.getValueAt(path) as? String ?: continue
            if (Cve2026_43284Fields.isTextInvalid(path, text)) invalid += path
        }
        for (path in Cve2026_43284Fields.UInt32Paths) {
            val value = (profile.getValueAt(path) as? Number)?.toLong() ?: continue
            if (value < 0L || value > Cve2026_43284Fields.UInt32Max) invalid += path
        }
        return invalid
    }

    /** True when the effective wire backend is cve_2026_43284. */
    private fun is43284Selection(): Boolean {
        combinationSelection?.invoke()?.let { return it.backend == BackendKind.Cve2026_43284 }
        return BackendKind.selectableOrFallback(backendSelection()) == BackendKind.Cve2026_43284
    }

    /**
     * Surfaces the editable 43284 policy/tuning surface for a 43284 selection
     * even when the resolved (43499) profile does not carry the section yet, so
     * the advanced editor can fill it in. A value already present is kept.
     */
    private fun complete43284Fields(profile: ValueMap) {
        if (!is43284Selection()) return
        val section = profile.mutableChild("backend").mutableChild("cve_2026_43284")
        for (path in Cve2026_43284Fields.EditablePaths) {
            val key = path.substringAfterLast('.')
            if (!section.containsKey(key)) section[key] = null
        }
    }

    /** The single branch key declared under "route" (string legacy allowed). */
    private fun routeNameOf(profile: ValueMap): String? {
        return when (val value = profile["route"]) {
            is String -> value.takeIf { it.isNotEmpty() && it != "null" }
            is Map<*, *> -> value.keys.filterIsInstance<String>()
                .firstOrNull { it in ProfileConfig.Routes }
            else -> null
        }
    }

    override suspend fun updateRoute(
        release: String,
        pair: CpuPair,
        route: String?,
    ): ProfileConfig {
        val override = readAdvancedOverride(release)
        if (route.isNullOrBlank()) {
            override.remove("route")
        } else {
            val existingBranch = override["route"].asValueMap()?.get(route).asValueMap()
                ?: routeBranchTemplate(route)
            override["route"] = valueMapOf(route to existingBranch)
            pruneOverrideBranches(override, "route", route)
        }
        writeAdvancedOverride(release, override)
        persistSnapshot(release, pair)
        return load(release, pair)
    }

    override suspend fun updateGeneral(
        release: String,
        pair: CpuPair,
        values: Map<String, Long>,
    ): ProfileConfig {
        val override = readAdvancedOverride(release)
        val execution = override.mutableChild("execution")
        for ((path, value) in values) {
            if (path.startsWith("execution.")) {
                execution.setValueAt(path.removePrefix("execution."), value)
            } else {
                override.setValueAt(path, value)
            }
        }
        writeAdvancedOverride(release, override)
        persistSnapshot(release, pair)
        return load(release, pair)
    }

    override suspend fun updateAdvanced(
        release: String,
        pair: CpuPair,
        values: Map<String, Any>,
    ): ProfileConfig {
        /* Rebuild the sparse override from scratch: only values that differ from
         * the baseline survive, so untouched fields (including stale entries
         * from older builds) can never stay highlighted. Strings (the S4 R4
         * 43284 policy paths) compare against the resolved baseline text; an
         * empty draft suppresses a baseline value so "absent" can be requested
         * explicitly, and is dropped when the baseline is already absent. */
        val baseline = resolveCurrent(release, pair, null, includeImported = true)
        if (baseline != null) {
            val rebuilt = valueMapOf()
            for ((path, raw) in values) {
                if (path.isEmpty() || path == "release" ||
                    path.startsWith("execution.selected_cpus")
                ) {
                    continue
                }
                when (raw) {
                    is String -> {
                        val text = raw.trim()
                        val current = baseline.getValueAt(path) as? String
                        if (current == null) {
                            if (text.isNotEmpty()) rebuilt.setValueAt(path, text)
                        } else if (text != current) {
                            rebuilt.setValueAt(path, text)
                        }
                    }

                    is Number -> {
                        val value = raw.toLong()
                        if (value != baseline.getLongAt(path)) rebuilt.setValueAt(path, value)
                    }
                }
            }
            /* The advanced editor carries neither the route choice nor the
             * selected CPUs (and may drop a route branch the baseline already
             * matches); keep both. */
            val current = readAdvancedOverride(release)
            current["route"].asValueMap()?.let { route -> rebuilt.putIfAbsent("route", route) }
            current["execution"].asValueMap()?.get("selected_cpus")?.let { cpus ->
                rebuilt.mutableChild("execution")["selected_cpus"] = cpus
            }
            writeAdvancedOverride(release, rebuilt)
            persistSnapshot(release, pair)
        }
        return load(release, pair)
    }

    override suspend fun reset(release: String, pair: CpuPair): ProfileConfig {
        writeAdvancedOverride(release, valueMapOf())
        return load(release, pair)
    }

    override suspend fun resetGeneral(release: String, pair: CpuPair): ProfileConfig {
        val override = readAdvancedOverride(release)
        override.remove("execution")
        writeAdvancedOverride(release, override)
        return load(release, pair)
    }

    override suspend fun resetAdvanced(release: String, pair: CpuPair): ProfileConfig {
        val override = readAdvancedOverride(release)
        override.keys.toList().filter { it != "execution" }.forEach(override::remove)
        writeAdvancedOverride(release, override)
        return load(release, pair)
    }

    override suspend fun clearSelectedCpus(release: String, pair: CpuPair): ProfileConfig {
        val override = readAdvancedOverride(release)
        val execution = override["execution"].asValueMap()
        execution?.remove("selected_cpus")
        if (execution != null && execution.isEmpty()) override.remove("execution")
        writeAdvancedOverride(release, override)
        return load(release, pair)
    }

    override suspend fun export(
        release: String,
        pair: CpuPair,
        documentUri: String,
    ): Boolean = try {
        persistSnapshot(release, pair)
        val snapshot = File(filesDir, snapshotName(release))
        if (!snapshot.isFile) return false
        val resolver = appContext.contentResolver
        val output = resolver.openOutputStream(documentUri.toUri(), "wt") ?: return false
        output.use { stream -> snapshot.inputStream().use { it.copyTo(stream) } }
        true
    } catch (_: Exception) {
        false
    }

    override suspend fun saveModified(release: String, pair: CpuPair): Boolean =
        saveResolved(release, pair, modifiedName(release))

    /** Renders the resolved profile (built-in + imported + overrides) as HOCON. */
    fun renderResolved(release: String, pair: CpuPair): String? {
        val resolved = resolveCurrent(
            release, pair, readAdvancedOverride(release), includeImported = true,
        ) ?: return null
        val view = resolved.copyValue().asValueMap() ?: return null
        view["schema_version"] = GHOSTLOCK_PROFILE_SCHEMA_VERSION
        view["release"] = release
        /* The CPU choice follows the device pair, it must not be frozen here. */
        view["execution"].asValueMap()?.remove("selected_cpus")
        fillRouteExecutionDefaults(view)
        trimRouteTuning(view)
        return HoconSupport.render(view)
    }

    /** Renders the resolved profile verbatim (no trimming) for debug dumps. */
    fun renderResolvedForDebug(release: String, pair: CpuPair): String? {
        val resolved = resolveCurrent(
            release, pair, readAdvancedOverride(release), includeImported = true,
        ) ?: return null
        val view = resolved.copyValue().asValueMap() ?: return null
        return HoconSupport.render(view)
    }

    /** Stores the rendered resolved profile under [fileName]. */
    fun saveResolved(release: String, pair: CpuPair, fileName: String): Boolean {
        val text = renderResolved(release, pair) ?: return false
        return runCatching { userProfiles.save(fileName, text) }.isSuccess
    }

    /** Sparse overrides stored for [release]; seeds an editing session. */
    fun overridesSnapshot(release: String): ValueMap =
        readAdvancedOverride(release).copyValue().asValueMap() ?: valueMapOf()

    /**
     * P1: stores or clears ONE plugin parameter override in the existing
     * advanced override tree (dotted path `plugin.<id>.params.<name>`). Null
     * clears it, so the descriptor's default applies again; an explicit value is
     * stored verbatim, even when it equals the default, because "the user chose
     * this" and "the plugin defaults to this" must stay distinguishable.
     */
    fun setPluginParam(release: String, id: String, name: String, value: PluginValue?) {
        val overrides = readAdvancedOverride(release)
        val pluginSection = overrides.mutableChild("plugin")
        val plugin = pluginSection.mutableChild(id)
        val params = plugin.mutableChild("params")
        if (value == null) {
            params.remove(name)
            if (params.isEmpty()) plugin.remove("params")
            if (plugin.isEmpty()) pluginSection.remove(id)
        } else {
            params[name] = when (value) {
                is PluginValue.UInt -> value.value.toLong()
                is PluginValue.Int -> value.value
                is PluginValue.Bool -> value.value
                is PluginValue.Str -> value.value
            }
        }
        writeAdvancedOverride(release, overrides)
    }

    /** P1: the explicit parameter overrides of one installed plugin. */
    fun pluginOverrides(
        release: String,
        id: String,
        descriptor: PluginDescriptor,
    ): Map<String, PluginValue> =
        PluginOverrides.params(readAdvancedOverride(release), id, descriptor)

    /** Replaces the sparse overrides stored for [release]. */
    fun replaceOverrides(release: String, override: ValueMap) {
        writeAdvancedOverride(release, override.copyValue().asValueMap() ?: valueMapOf())
    }

    private fun modifiedName(release: String): String =
        "${release.replace(Regex("[^A-Za-z0-9._-]"), "_")}-modified.conf"

    override suspend fun builtinReleases(): List<String> {
        val index = readIndex() ?: return emptyList()
        val profiles = index["profiles"].asValueList() ?: return emptyList()
        return profiles
            .mapNotNull { entry -> (entry.asValueMap()?.get("release") as? String) }
            .filter { it.isNotEmpty() }
            .sorted()
    }

    override fun activeBuiltinRelease(): String? =
        forcedBuiltinRelease
            ?: preferences.getString(PrefBuiltinRelease, null)?.takeIf { it.isNotEmpty() }

    override fun activeUserProfile(): String? =
        forcedUserProfile
            ?: preferences.getString(PrefActiveUserProfile, null)?.takeIf { it.isNotEmpty() }

    override suspend fun selectUserProfile(
        name: String?,
        deviceRelease: String,
        pair: CpuPair,
    ): ProfileConfig {
        preferences.edit {
            if (name.isNullOrBlank()) remove(PrefActiveUserProfile)
            else putString(PrefActiveUserProfile, name)
        }
        return load(deviceRelease, pair)
    }

    override fun onUserProfileRenamed(oldName: String, newName: String) {
        if (activeUserProfile() == oldName) {
            preferences.edit { putString(PrefActiveUserProfile, newName) }
        }
    }

    override fun onUserProfileDeleted(name: String) {
        if (activeUserProfile() == name) {
            preferences.edit { remove(PrefActiveUserProfile) }
        }
    }

    override suspend fun selectBuiltin(
        release: String?,
        deviceRelease: String,
        pair: CpuPair,
    ): ProfileConfig {
        preferences.edit {
            if (release.isNullOrBlank()) remove(PrefBuiltinRelease)
            else putString(PrefBuiltinRelease, release)
        }
        return load(deviceRelease, pair)
    }

    /**
     * Production wire: the resolved logical document is translated to the
     * typed GLKv3 document and written as canonical MessagePack, the format the
     * native reader consumes (GLKv3-4).
     */
    override fun nativeDocument(config: ProfileConfig): ByteArray? = synchronized(lock) {
        if (cachedRelease != config.release) return@synchronized null
        cachedProfile?.let { profile ->
            Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(profile.document))
        }
    }

    /** The sparse triple the live UI selection addresses. */
    private fun resolvedSelection(): ExecutionSelection =
        resolveExecutionSelection(executionModeSelection(), backendSelection())

    /** Builds the single resolved authority native consumes at run time. */
    private fun buildNativeDocument(release: String, profile: ValueMap): Profile? {
        val route = routeNameOf(profile)
        /* Backend choice is an app-level preference, not profile text: inject the
         * selected token so NativeProfileDocument.from reads it from the same
         * `backend.kind` path the exporter and imported profiles already use. The
         * copy keeps the resolved HOCON (editor tree, exports) free of the
         * app-only selection. */
        val resolved = profile.copyValue().asValueMap() ?: profile
        val backend = resolved.mutableChild("backend")
        val combination = combinationSelection?.invoke()
        if (combination != null) {
            /* S4 R6b: the single token is the selection authority; write exactly
             * one token into backend.<id>.steps and derive the backend from it. */
            backend["kind"] = combination.backend.token
            backend["steps"] = combination.token
        } else {
            val selection = resolvedSelection()
            backend["kind"] = selection.backend.token
            /* Legacy mode/backend pair: 43284's sparse triple is fixed, so inject
             * its token resolved from the manifest (F4: no token literal here);
             * 43499 keeps its profile-owned token. */
            if (selection.backend == BackendKind.Cve2026_43284) {
                CombinationCatalog.fromDerived(
                    selection.backend, selection.steps, selection.terminal,
                )?.let { backend["steps"] = it.token }
            }
        }
        return Profile.fromValueMap(
            release = release,
            route = RouteKind.resolve(RouteKind.normalize(route)),
            text = { path -> ProfileResolver.nativeText(resolved, path) },
            bool = { path -> ProfileResolver.nativeBool(resolved, path) },
            value = { path -> ProfileResolver.nativeValue(resolved, route, path) },
            /* P1: only enabled plugins, and only the parameters the user
             * explicitly overrode (the descriptor keeps the defaults). The
             * override tree is the controller's, so it is applied here. */
            plugins = pluginSelection?.invoke().orEmpty().map { enabled ->
                requireNotNull(
                    PluginEmission.of(
                        enabled.entry,
                        enabled.descriptor,
                        PluginOverrides.params(
                            overridesSnapshot(release),
                            enabled.entry.id,
                            enabled.descriptor,
                        ),
                    ),
                ) { "enabled plugin produced no emission: " + enabled.entry.id }
            },
        )
    }

    // ---- resolution (migrated from ProfileConfiguration) ----

    /** Resolves through the currently selected builtin release (if any). */
    private fun resolveCurrent(
        deviceRelease: String,
        pair: CpuPair,
        overrides: ValueMap?,
        includeImported: Boolean,
    ): ValueMap? {
        val profileRelease = activeBuiltinRelease() ?: deviceRelease
        return resolve(deviceRelease, profileRelease, pair, overrides, includeImported)
    }

    private fun resolve(
        deviceRelease: String,
        profileRelease: String,
        pair: CpuPair,
        overrides: ValueMap?,
        includeImported: Boolean,
    ): ValueMap? = runCatching {
        val index = readIndex() ?: return@runCatching null
        LegacyProfileConverter.normalizeSchemaVersion(
            index["schema_version"], "index.conf",
        )
        val builtinEntry = findProfile(index["profiles"].asValueList(), profileRelease)
        val imported = if (includeImported) {
            userProfiles.loadEntry(deviceRelease, activeUserProfile())
        } else {
            null
        }
        LegacyProfileConverter.convertValue(overrides)
        if (builtinEntry == null && imported == null) return@runCatching null
        val builtin = builtinEntry?.let { entry ->
            val path = (entry["file"] as? String).orEmpty()
            (HoconSupport.parseValue(readAsset("$BuiltinDirectory/$path")).asValueMap()
                ?: error("profile is not an object"))
                .also {
                    /* R3: normalize the canonical owner-qualified layout (or a
                     * legacy flat document) at parse time. */
                    ProfileLayout.applyNormalize(it)
                    LegacyProfileConverter.normalizeSchemaVersion(
                        it["schema_version"], "$BuiltinDirectory/$path",
                    )
                    require(it["release"] == entry["release"]) {
                        "profile index release mismatch"
                    }
                }
        }
        val tuningExecution = readExecutionTuning()?.get("execution").asValueMap()
        val routePresets = ProfileConfig.Routes
            .mapNotNull { route -> readExecutionRoute(route)?.let { route to it } }
            .toMap()
        ProfileMerger.resolveMerged(
            deviceRelease = deviceRelease,
            builtin = builtin,
            imported = imported,
            overrides = overrides,
            tuningExecution = tuningExecution,
            pair = CpuPairView(pair.primary, pair.consumer),
            routePresets = routePresets,
        )
    }.getOrNull()

    /**
     * Native decodes one complete `execution.routes` object, while profiles only
     * include the route they use. Missing groups are
     * filled from the shared `execution-<route>.conf` files.
     */
    private fun fillRouteExecutionDefaults(profile: ValueMap) {
        val presets = ProfileConfig.Routes
            .mapNotNull { route -> readExecutionRoute(route)?.let { route to it } }
            .toMap()
        ProfileMerger.fillRouteExecutionDefaults(profile, presets)
    }

    private fun readExecutionRoute(route: String): ValueMap? = runCatching {
        HoconSupport.parseValue(
            readAsset("$BuiltinDirectory/execution-${route.replace('_', '-')}.conf"),
        ).asValueMap()?.also { ProfileLayout.applyNormalize(it) }
            ?.get("execution").asValueMap()?.get("routes").asValueMap()?.get(route).asValueMap()
    }.getOrNull()

    private fun readIndex(): ValueMap? = runCatching {
        HoconSupport.parseValue(readAsset("$BuiltinDirectory/index.conf")).asValueMap()
            ?: error("index.conf is not an object")
    }.getOrNull()

    private fun findProfile(profiles: List<*>?, release: String): ValueMap? =
        profiles.orEmpty().asSequence()
            .mapNotNull { it.asValueMap() }
            .firstOrNull { it["release"] == release }

    private fun readAsset(path: String): String = assetLoader.load(path)

    /** Shared execution tuning every profile includes ("execution-tuning.conf"). */
    private fun readExecutionTuning(): ValueMap? = runCatching {
        HoconSupport.parseValue(readAsset("$BuiltinDirectory/execution-tuning.conf"))
            .asValueMap()?.also { ProfileLayout.applyNormalize(it) }
    }.getOrNull()

    /** Creates null placeholders for invalid fields the document does not carry. */
    private fun materializeInvalidPaths(profile: ValueMap, invalidPaths: Set<String>) {
        invalidPaths.forEach { path ->
            val segments = path.split('.')
            var node: ValueMap = profile
            for (index in 0 until segments.size - 1) {
                node = node.mutableChild(segments[index])
            }
            if (!node.containsKey(segments.last())) node[segments.last()] = null
        }
    }

    /** Initialises a switched-to branch with its fields so they can be filled. */
    private fun routeBranchTemplate(route: String): ValueMap {
        val template = valueMapOf()
        RouteBranchFields[RouteKind.resolve(route)].orEmpty().forEach { template[it] = null }
        return template
    }

    /**
     * After a route switch only the selected branch survives in the advanced
     * override; edits of the previous branch would otherwise pull the choice
     * back or shadow the new branch's fields.
     */
    private fun pruneOverrideBranches(entry: ValueMap, key: String, keep: String?) {
        val container = entry[key].asValueMap() ?: return
        container.keys.toList().filter { it != keep }.forEach(container::remove)
        if (container.isEmpty()) entry.remove(key)
    }

    // ---- controller model helpers ----

    private fun generalFields(
        profile: ValueMap,
        baseline: ValueMap,
        route: String?,
    ): List<ExecutionFieldValue> {
        fun read(root: ValueMap, path: String): Long? = if (path.startsWith("execution.")) {
            root["execution"].asValueMap()?.getLongAt(path.removePrefix("execution."))
        } else {
            root.getLongAt(path)
        }
        /* Route tuning is appended from the resolved document itself: only the
         * active route's leaves (sorted, so the order is stable), so the editor
         * never offers another route's knobs. The group is filled into
         * `execution.routes` during resolution, so the keys come straight from
         * the HOCON. */
        val paths = ProfileConfig.GeneralPaths +
            routeTuningPaths(profile, route)
        return paths.map { path ->
            val value = read(profile, path) ?: 0L
            ExecutionFieldValue(
                path = path,
                value = value,
                overridden = read(baseline, path)?.let { baselineValue -> value != baselineValue } == true,
            )
        }
    }

    private fun routeTuningPaths(profile: ValueMap, route: String?): List<String> {
        if (route == null || route !in ProfileConfig.Routes) return emptyList()
        val group = profile["execution"].asValueMap()?.get("routes").asValueMap()
            ?.get(route).asValueMap() ?: return emptyList()
        return group.keys.filterIsInstance<String>().sorted()
            .map { "execution.routes.$route.$it" }
    }

    /**
     * Fills every field of the shared geometry and the active route with an
     * explicit `null` when the resolved profile omitted it, so the advanced
     * editor always presents the complete editable surface.
     */
    private fun completeProfileFields(
        profile: ValueMap,
        route: String?,
    ): ValueMap {
        val out = profile.copyValue().asValueMap() ?: return profile
        if (!out.containsKey("kernel_phys_load")) out["kernel_phys_load"] = null
        if (!out.containsKey("kernel_phys_offset")) out["kernel_phys_offset"] = null
        completeSection(out, "task_struct", TaskStructFieldNames)
        completeSection(out, "cred", CredFieldNames)
        completeSection(out, "offset", OffsetFieldNames)
        completeSection(out, "kernelsnitch", KernelsnitchFieldNames)
        route?.let { completeRouteBranch(out["route"].asValueMap(), it) }
        return out
    }

    private fun completeSection(out: ValueMap, section: String, fields: List<String>) {
        val target = out[section].asValueMap() ?: valueMapOf().also { out[section] = it }
        for (field in fields) if (!target.containsKey(field)) target[field] = null
    }

    private fun completeRouteBranch(container: ValueMap?, route: String) {
        val fields = RouteBranchFields[RouteKind.resolve(route)] ?: return
        val containerMap = container ?: return
        val branch = containerMap[route].asValueMap()
            ?: valueMapOf().also { containerMap[route] = it }
        for (field in fields) if (!branch.containsKey(field)) branch[field] = null
    }

    /** True when the GLKv3 manifest declares [path] as a wire `str` field. */
    private fun isStringFieldPath(path: String): Boolean =
        NativeProfileGlkv3Adapter.declaredWire(path) ==
            NativeProfileGlkv3Adapter.WireType.Str

    private fun buildTree(
        node: ValueMap,
        prefix: String,
        baseline: ValueMap,
        override: ValueMap,
    ): List<ProfileFieldNode> {
        val groups = mutableListOf<ProfileFieldNode>()
        val leaves = mutableListOf<ProfileFieldNode>()
        for ((key, value) in node) {
            val path = if (prefix.isEmpty()) key else "$prefix.$key"
            when {
                value is Map<*, *> -> {
                    /* Execution tuning is edited on the general page
                     * (ProfileOverrideScreen / ExecutionEditor), not here. */
                    if (key == "execution") continue
                    val children = buildTree(value.asValueMap() ?: valueMapOf(), path, baseline, override)
                    if (children.isNotEmpty()) {
                        groups += ProfileFieldNode(
                            path = path,
                            name = key,
                            overridden = children.any { it.overridden },
                            children = children,
                        )
                    }
                }

                value is Number -> if (path != "schema_version" && path != "release") {
                    val overrideValue = override.getLongAt(path)
                    leaves += ProfileFieldNode(
                        path = path,
                        name = key,
                        value = value.toLong(),
                        overridden = overrideValue != null &&
                            overrideValue != baseline.getLongAt(path),
                    )
                }

                /* HOCON booleans surface as an editable 1/0 leaf. */
                value is Boolean -> if (path != "schema_version" && path != "release") {
                    val overrideValue = override.getLongAt(path)
                    leaves += ProfileFieldNode(
                        path = path,
                        name = key,
                        value = if (value) 1L else 0L,
                        overridden = overrideValue != null &&
                            overrideValue != baseline.getLongAt(path),
                    )
                }

                /* S4 R4 string leaves (the 43284 policy paths). Only paths the
                 * GLKv3 manifest declares as wire `str` are editable here;
                 * other HOCON strings (backend.steps/kind) are
                 * selection tokens owned by their dedicated controls. */
                value is String -> if (path != "schema_version" && path != "release" &&
                    isStringFieldPath(path)
                ) {
                    val overrideValue = override.getValueAt(path) as? String
                    leaves += ProfileFieldNode(
                        path = path,
                        name = key,
                        textValue = value,
                        overridden = overrideValue != null &&
                            overrideValue != (baseline.getValueAt(path) as? String),
                    )
                }

                value == null -> if (path != "schema_version" && path != "release") {
                    leaves += ProfileFieldNode(
                        path = path,
                        name = key,
                        value = null,
                        overridden = override.getValueAt(path) != null,
                    )
                }
            }
        }
        return groups.sortedBy { it.name } + leaves.sortedBy { it.name }
    }

    // ---- persistence ----

    private fun cache(release: String, profile: Profile?) = synchronized(lock) {
        cachedRelease = release
        cachedProfile = profile
    }

    private fun readDebugOverrides(): ValueMap {
        val raw = preferences.getString(PrefDebugProfileOverrides, null) ?: return valueMapOf()
        return runCatching { HoconSupport.parseValue(raw).asValueMap() ?: valueMapOf() }
            .getOrDefault(valueMapOf())
    }

    /** Sparse overrides for [release]: general, route and advanced. */
    private fun readAdvancedOverride(release: String): ValueMap =
        readDebugOverrides()[release].asValueMap() ?: valueMapOf()

    private fun writeAdvancedOverride(release: String, override: ValueMap) {
        val all = readDebugOverrides()
        if (override.isEmpty()) all.remove(release) else all[release] = override
        preferences.edit { putString(PrefDebugProfileOverrides, HoconSupport.render(all)) }
    }

    /* profile-export: the merged profile also lives as a plain HOCON file in the
     * app-private directory, so an export is a copy, never a re-merge. */
    private fun persistSnapshot(release: String, pair: CpuPair) {
        runCatching {
            val resolved = resolveCurrent(
                release, pair, readAdvancedOverride(release), includeImported = true,
            ) ?: return
            val exportView = resolved.copyValue().asValueMap() ?: return
            /* Renderer-side completeness: pull in the tuning of the selected
             * route, then drop the groups that are not used. */
            fillRouteExecutionDefaults(exportView)
            trimRouteTuning(exportView)
            File(filesDir, snapshotName(release))
                .writeText(HoconSupport.render(exportView), StandardCharsets.UTF_8)
            cache(release, buildNativeDocument(release, resolved))
        }.onFailure {
            android.util.Log.e("GhostLock", "persistSnapshot failed for $release", it)
        }
    }

    private fun snapshotName(release: String): String =
        "${release.replace(Regex("[^A-Za-z0-9._-]"), "_")}.conf"

    /** The exported document keeps only the selected route's tuning. */
    private fun trimRouteTuning(profile: ValueMap) {
        val routes = profile["execution"].asValueMap()?.get("routes").asValueMap() ?: return
        val keep = buildSet {
            routeNameOf(profile)?.let(::add)
        }
        routes.keys.toList().filter { it !in keep }.forEach(routes::remove)
    }

    internal companion object {
        private const val BuiltinDirectory = "kernel_profiles"
        private val RouteCommonRequired = listOf(
            "offset.init_task", "offset.init_cred", "offset.root_task_group", "offset.selinux_enforcing",
            "task_struct.prio", "task_struct.pi_lock", "task_struct.pi_waiters", "task_struct.pi_blocked_on",
            "task_struct.cred", "task_struct.seccomp",
        )
        /**
         * Optional poison/walk tuning and the native field widths (u8/u8/u16).
         * A value outside the width is reported instead of being wrapped by the
         * typed cast, per the route-config contract.
         */
        private val RouteMulticastTuning = listOf(
            "attempts" to 0xffL, "arm_sequence" to 0xffL, "arm_hold" to 0xffffL,
        )
        /** Full shared-geometry field universes (native `kTask`/`kCred`/`kOffset`). */
        private val TaskStructFieldNames = listOf(
            "prio", "normal_prio", "sched_task_group", "pi_lock", "pi_waiters", "pi_top_task",
            "pi_blocked_on", "pid", "tgid", "atomic_flags", "real_cred", "cred", "comm", "tasks",
            "seccomp",
        )
        private val CredFieldNames = listOf(
            "copy_size", "usage_offset", "usage_value", "caps_offset", "caps_count", "caps_value",
            "ref_count", "ref0_offset", "ref1_offset", "ref2_offset", "ref3_offset",
            "ref0_image", "ref1_image", "ref2_image", "ref3_image",
        )
        private val OffsetFieldNames = listOf(
            "init_task", "init_cred", "empty_zero_page", "root_task_group", "selinux_enforcing",
            "selinux_blob_sizes", "security_hook_heads", "slide_nfulnl_logger", "slide_loggers_0_1",
            "slide_boot_id",
        )
        private val KernelsnitchFieldNames = listOf("collisions", "mm_struct_sz")
        /** Fields each route branch carries, used to seed a switched-to route. */
        private val RouteBranchFields = mapOf(
            RouteKind.TCP_ZEROCOPY to listOf("compact_waiter"),
            RouteKind.SELECT_STACK to listOf("waiter_shift"),
            RouteKind.MULTICAST_WAITER to listOf(
                "waiter_off", "buffer_size", "task_offset", "lock_offset",
                "compact_waiter",
            ) + RouteMulticastTuning.map { it.first },
        )
        private val RouteMulticastFields = listOf(
            "buffer_size", "task_offset", "lock_offset",
        )
        private const val SizeofU32 = 4L
        private const val SizeofU64 = 8L
        private const val PrefBuiltinRelease = "debug_builtin_release"
        private const val PrefActiveUserProfile = "active_user_profile"
        const val PrefDebugProfileOverrides = "debug_profile_overrides"
    }
}
