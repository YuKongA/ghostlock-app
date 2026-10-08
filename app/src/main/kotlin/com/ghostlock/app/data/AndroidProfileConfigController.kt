package com.ghostlock.app.data


import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import androidx.core.net.toUri
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.component.stepNames
import com.ghostlock.app.data.plugin.EnabledPlugin
import com.ghostlock.app.data.plugin.PluginDescriptor
import com.ghostlock.app.data.plugin.PluginEmission
import com.ghostlock.app.data.plugin.PluginOverrides
import com.ghostlock.app.data.plugin.PluginSelection
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
 * (b1) The named refusal for a selected combination the profile does NOT declare,
 * or null when the selection is legal. SINGLE AUTHORITY: the emitter and the tests
 * both read this function, so the rule cannot drift into a second copy.
 *
 * [declaration] is the `available` map captured before normalization (the runtime
 * form has none), and the combination is matched on (backend, route, stepNames) -
 * the same key the selection projection uses.
 */
/**
 * (B-ii) The map the must-have judgement is run against: the runtime form with the
 * DECLARATION (captured before normalization, official unwrap upstream) injected.
 * Pure and side-effect free - the caller keeps its own map (materializeInvalidPaths
 * still works on the original). Extracted so the visibility of the declaration is
 * testable: without it the runtime map has no `available` and every declaration-
 * driven rule is silently skipped. Same authority, no new rule.
 */
internal fun judgingProfileWithDeclaration(full: ValueMap, declaration: ValueMap): ValueMap =
    if (declaration.isEmpty()) {
        full
    } else {
        full.copyValue().asValueMap()?.also { it["available"] = declaration } ?: full
    }

internal fun declarationRefusalFor(
    declaration: ValueMap,
    combination: CombinationSpec,
): String? {
    val route = combination.route?.token
    val steps = runCatching { combination.steps.stepNames() }.getOrNull()
    val declared = declaredCombinations(declaration).any { path ->
        path.backend == combination.backend.token &&
            path.route == route &&
            path.steps == steps
    }
    if (declared) return null
    return "execution-combo: the selected combination is not declared by this profile: " +
        combination.backend.token + (route?.let { "/" + it } ?: "") +
        " steps=" + combination.steps.token +
        "; the available declaration is the selection surface - declare this " +
        "path or choose a declared one"
}

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
    /**
     * The backend the USER picked, or null when they never picked one. Null is
     * meaningful since design 2.9/U17: the available declaration is then the
     * selection surface and the backend is derived through AvailablePriority
     * (an explicit pick still wins - design 1-prime).
     */
    private val backendSelection: (() -> BackendKind?)? = null,
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
    /*
     * COMMENTED OUT (user ruling 2026-10-05): the plugin run selection is
     * withdrawn while the plugin design is redone, so the controller neither
     * resolves plugins nor emits them. Restore = uncomment this parameter, the
     * selection/emission block in buildNativeDocument, the `plugins =` argument
     * there, and the wiring in AndroidGhostlockRepository.
     *
     * P1: the ENABLED plugins (registry row + probe descriptor) the repository
     * resolved. The controller applies the user's overrides to them, because the
     * override store is the controller's. Null/empty keeps every existing caller
     * byte-identical.
     */
    // private val pluginSelection: (suspend () -> PluginSelection)? = null,
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
        val resolved = resolveCurrentResolved(deviceRelease, pair, advanced, includeImported = true)
        val full = resolved?.runtime
        if (full == null) {
            cache(deviceRelease, null)
            return ProfileConfig(
                release = deviceRelease,
                hasProfile = false,
                /* No profile resolved: there genuinely are no declared combinations. */
                declaredCombinations = emptyList(),
            )
        }
        /* (B-ii) the DECLARATION comes from the captured layers (window 1), not
         * from the runtime map: the runtime projection drops `available`, so
         * reading it there yielded an empty list (the UI then fell back to the
         * whole catalogue - the A301SO complaint). */
        val declaration = resolved.declaration
        val baseline = resolveCurrent(deviceRelease, pair, null, includeImported = false)
            ?: full
        val route = routeNameOf(full)
        /* The must-have rules are judged against what the profile DECLARES (design
         * 2.9-1: declares43499), but the runtime map has no `available` - so the
         * 43499-specific rules were silently skipped and e.g. offset.init_task = 0
         * stayed legal. Inject the DECLARATION (captured before normalization,
         * official unwrap upstream) into a copy: same authority, no new rule, and the
         * runtime values are preserved. `full` itself is left to
         * materializeInvalidPaths exactly as before. */
        val judgingProfile = judgingProfileWithDeclaration(full, declaration)
        val invalidPaths = (validateProfileFields(judgingProfile, route) +
            validate43284Fields(judgingProfile) +
            ProfileResolver.validateMerged(judgingProfile, route).mapTo(mutableSetOf()) { it.fieldPath })
            /* `available` is the INTERNAL declaration carrier injected above so the
             * must-have rules can see what the profile declares (design 2.9-1); it is
             * not a user field and must never surface as an invalid path. */
            .filterNotTo(mutableSetOf()) { it == "available" || it.startsWith("available.") }
        /* Invalid fields missing from the resolved document still get a row,
         * otherwise the run stays blocked with no red field to fix. */
        materializeInvalidPaths(full, invalidPaths)
        /* The editor shows every field of the active route plus the shared
         * geometry, so a field the profile did not carry appears as an
         * editable `null` row instead of being invisible. */
        val complete = completeProfileFields(full, route)
        complete43284Fields(complete)
        val roots = buildTree(complete, "", baseline, advanced)
        val built = buildNativeDocument(deviceRelease, full, declaration)
        cache(deviceRelease, built.profile)
        return ProfileConfig(
            release = deviceRelease,
            hasProfile = true,
            roots = roots,
            general = generalFields(full, baseline, route),
            route = route,
            invalidPaths = invalidPaths,
            pluginErrors = built.pluginErrors,
            declarationErrors = built.declarationErrors,
            /* Design 2.9/U17: the DECLARED combinations are projected for the
             * selection UI, so the menu can never list a path the profile does
             * not declare (that was the A301SO complaint). The canonical map is
             * the only source; when it is unavailable the list stays EMPTY - it
             * never means "all of the catalogue". */
            declaredCombinations = declaredCombinations(declaration),
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

        /* Design 2.9-1 (shared judgement, single source): the must-have parameters
         * belong to the paths a profile DECLARES usable. A general profile that
         * declares only cve_2026_43284 legitimately carries no 43499 route / ABI
         * geometry, so those requirements do not apply to it; a profile that
         * declares 43499 is held to every rule below (zero relaxation). This is the
         * SAME helper the exporter path uses - one criterion, two call sites. */
        val needs43499 = ProfileResolver.declaresBackend(profile, "cve_2026_43499")
        /* The route is 43499 profile-controlled: a missing or unknown branch is
         * invalid. Legacy documents get their route from the converter, so an
         * unresolved route here means the profile is genuinely broken. */
        val route = explicitRoute?.takeIf { it in ProfileConfig.Routes }
        if (needs43499 && route == null) invalid += "route"

        if (needs43499) requireNonZero(*RouteCommonRequired.toTypedArray())
        val major = value("kernel_major")
        if (major != 5L && major != 6L) invalid += "kernel_major"

        /* Everything below is 43499-specific (its ABI template and route tuning).
         * A profile that does not declare 43499 usable is done here: judging it by
         * 43499 parameters would report fields it must not carry (design 2.9-1). */
        if (!needs43499) return invalid

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
        if (!is43284Selection(profile)) return emptySet()
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
    private fun is43284Selection(profile: ValueMap): Boolean {
        combinationSelection?.invoke()?.let { return it.backend == BackendKind.Cve2026_43284 }
        return effectiveBackend(profile) == BackendKind.Cve2026_43284
    }

    /**
     * Surfaces the editable 43284 policy/tuning surface for a 43284 selection
     * even when the resolved (43499) profile does not carry the section yet, so
     * the advanced editor can fill it in. A value already present is kept.
     */
    private fun complete43284Fields(profile: ValueMap) {
        if (!is43284Selection(profile)) return
        val section = profile.mutableChild("backend").mutableChild("cve_2026_43284")
        for (path in Cve2026_43284Fields.EditablePaths) {
            /* Walk the whole owner-qualified path below the section so a nested
             * field such as "...cve_2026_43284.execution.wait_timeout_ms" lands on
             * its own level; taking only the last segment used to surface it one
             * level up, where the advanced editor could not address it. */
            val relative = path.removePrefix("${Cve2026_43284Fields.Section}.")
            val segments = relative.split('.')
            val node = segments.dropLast(1).fold(section) { current, segment ->
                current.mutableChild(segment)
            }
            val key = segments.last()
            if (!node.containsKey(key)) node[key] = null
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
        /* ONE addressing scheme: the override tree IS the document shape, so a
         * dotted path is the address, exactly as the field constants spell it.
         * The old \`execution.\` special case was a second convention that made
         * owner-qualified paths (backend.cve_2026_43284.execution.*) unwritable. */
        for ((path, value) in values) {
            override.setValueAt(path, value)
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
        setPluginGroup(release, id, "params", name, value)
    }

    /** Shared writer for `plugin.<id>.<group>.<name>` (params and extract). */
    private fun setPluginGroup(
        release: String,
        id: String,
        groupKey: String,
        name: String,
        value: PluginValue?,
    ) {
        val overrides = readAdvancedOverride(release)
        val pluginSection = overrides.mutableChild("plugin")
        val plugin = pluginSection.mutableChild(id)
        val group = plugin.mutableChild(groupKey)
        if (value == null) {
            group.remove(name)
            if (group.isEmpty()) plugin.remove(groupKey)
            if (plugin.isEmpty()) pluginSection.remove(id)
        } else {
            group[name] = when (value) {
                is PluginValue.UInt -> value.value.toLong()
                is PluginValue.Int -> value.value
                is PluginValue.Bool -> value.value
                is PluginValue.Str -> value.value
            }
        }
        writeAdvancedOverride(release, overrides)
    }

    /**
     * P1: stores or clears one extractor value (P2) in the same override tree,
     * at `plugin.<id>.extract.<key>`. Null clears it, so an unresolved key is
     * simply not emitted.
     */
    fun setPluginExtract(release: String, id: String, name: String, value: PluginValue?) {
        setPluginGroup(release, id, "extract", name, value)
    }

    /** P1: the explicit parameter overrides of one installed plugin. */
    fun pluginOverrides(
        release: String,
        id: String,
        descriptor: PluginDescriptor,
    ): Map<String, PluginValue> =
        PluginOverrides.params(readAdvancedOverride(release), id, descriptor)

    /**
     * Drops every override of one plugin (both groups) so the descriptor's
     * defaults apply again. Only \`plugin.<id>.\` is touched.
     */
    fun clearPluginOverrides(release: String, id: String) {
        val overrides = readAdvancedOverride(release)
        val pluginSection = overrides.mutableChild("plugin")
        pluginSection.remove(id)
        if (pluginSection.isEmpty()) overrides.remove("plugin")
        writeAdvancedOverride(release, overrides)
    }

    /** P2: the extractor values of one installed plugin. */
    fun pluginExtracts(
        release: String,
        id: String,
        descriptor: PluginDescriptor,
    ): Map<String, PluginValue> =
        PluginOverrides.extract(readAdvancedOverride(release), id, descriptor)

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
        resolveExecutionSelection(
            executionModeSelection(),
            backendSelection?.invoke() ?: BackendKind.Default,
        )

    /**
     * The effective backend of one profile: the user preference when set (1-prime),
     * otherwise DERIVED from the available declaration through the shared
     * AvailablePriority authority. A 43284-only profile therefore yields a 43284
     * document - before this, an unrelated default overrode the declaration and the
     * declared 43284 queue never reached the wire.
     */
    private fun effectiveBackend(
        profile: ValueMap,
        /* The DECLARATION captured before normalization (the runtime map has no
         * `available`). Deriving from `profile` alone returned null - the runtime form
         * simply has no declaration - so the fallback silently owned the wire
         * (BackendKind.Default = 43499) and the declared default never reached it.
         * Declaration first, runtime second: same authority (AvailablePriority), no
         * new rule. A document that declares nothing keeps the previous behaviour. */
        declaration: ValueMap = ValueMap(),
    ): BackendKind {
        val preference = backendSelection?.invoke()
        /* selectedBackend reads the `available` KEY from the map it is given, so the
         * declaration must be INJECTED into a runtime copy - passing the declaration
         * contents as the profile made that lookup miss and the call returned null
         * (the same shape mistake as reading a wrapped HOCON root). The copy keeps the
         * runtime carrier / backend.kind fallback effective and never mutates the
         * caller map. Same authority, no new rule. */
        /* `declaration` is already a document carrying `available` (see the capture
         * convention above), so no hand-rolled wrapping is needed any more: derive
         * from it first, then from the runtime profile (carrier / backend.kind). */
        val derived = AvailablePriority.selectedBackend(declaration, preference?.token)
            ?: AvailablePriority.selectedBackend(profile, preference?.token)
        return derived?.let { BackendKind.resolve(it) } ?: preference ?: BackendKind.Default
    }

    /**
     * One document build: the resolved [profile] native consumes (null when the
     * build is blocked) plus the user-visible [pluginErrors] that blocked it.
     */
    /**
     * One parsed layer before normalization: the runtime profile plus the
     * DECLARATION (`available`) it declared (B-ii). The declaration is captured
     * here because the runtime projection drops it.
     */
    private data class BuiltinLayer(val runtime: ValueMap, val declaration: ValueMap?)

    /**
     * The resolved profile in BOTH forms: the runtime map every existing consumer
     * uses, and the merged declaration the selection surface needs.
     */
    private data class ResolvedProfile(val runtime: ValueMap, val declaration: ValueMap)

    private data class NativeDocument(
        val profile: Profile?,
        val pluginErrors: List<String>,
        /* (b1) A selected combination the profile does not DECLARE. Named, never
         * silently remapped: the run gate refuses while this is non-empty. */
        val declarationErrors: List<String> = emptyList(),
    )

    /**
     * Builds the single resolved authority native consumes at run time.
     *
     * A plugin problem is a RESULT, never an exception: this runs on every
     * profile load (a hot path), and the old `error(...)` for a selected plugin
     * without a descriptor crashed the process there. A selected plugin that
     * cannot be emitted now yields [NativeDocument.pluginErrors] instead.
     */
    private suspend fun buildNativeDocument(
        release: String,
        profile: ValueMap,
        /* The DECLARATION captured before normalization (B-ii): the runtime map
         * has no `available`, so it can never answer this question. */
        declaration: ValueMap,
    ): NativeDocument {
        val route = routeNameOf(profile)
        /* Backend choice is an app-level preference, not profile text: inject the
         * selected token so NativeProfileDocument.from reads it from the same
         * `backend.kind` path the exporter and imported profiles already use. The
         * copy keeps the resolved HOCON (editor tree, exports) free of the
         * app-only selection. */
        val resolved = profile.copyValue().asValueMap() ?: profile
        val backend = resolved.mutableChild("backend")
        val declarationErrors = mutableListOf<String>()
        val combination = combinationSelection?.invoke()
        if (combination != null) {
            /* S4 R6b: the single token is the selection authority; write exactly
             * one token into backend.<id>.steps and derive the backend from it. */
            backend["kind"] = combination.backend.token
            backend["steps"] = combination.token
            /* (b1) fail-closed, never a silent fallback: a combination the profile
             * does not declare cannot run. Judged against the DECLARATION captured
             * before normalization (the runtime map has no `available`), so an
             * undeclared pick is refused by name instead of being quietly replaced. */
            declarationRefusalFor(declaration, combination)?.let { declarationErrors += it }
        } else {
            val selection = resolveExecutionSelection(
                executionModeSelection(),
                effectiveBackend(profile, declaration),
            )
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
        /* COMMENTED OUT (user ruling 2026-10-05): plugin resolution + emission are
         * withdrawn while the plugin design is redone. Nothing resolves the
         * selection, so the probe never runs and no plugin.* field is written.
         * Restore by uncommenting this block and the `plugins = emissions`
         * argument below. */
        // val selected = when (val selection = pluginSelection?.invoke()) {
        //     /* A SELECTED plugin the probe could not describe: the document
        //      * stays unbuilt and the reasons travel to the UI and the run gate. */
        //     is PluginSelection.Blocked -> return NativeDocument(null, selection.reasons)
        //     is PluginSelection.Ready -> selection.plugins
        //     null -> emptyList()
        // }
        // val pluginErrors = mutableListOf<String>()
        // /* P1: only enabled plugins, and only the parameters the user explicitly
        //  * overrode (the descriptor keeps the defaults). */
        // val emissions = selected.mapNotNull { enabled ->
        //     runCatching {
        //         PluginEmission.of(
        //             enabled.entry,
        //             enabled.descriptor,
        //             PluginOverrides.params(
        //                 overridesSnapshot(release),
        //                 enabled.entry.id,
        //                 enabled.descriptor,
        //             ),
        //             PluginOverrides.extract(
        //                 overridesSnapshot(release),
        //                 enabled.entry.id,
        //                 enabled.descriptor,
        //             ),
        //         )
        //     }.getOrElse { error ->
        //         pluginErrors += "plugin " + enabled.entry.id + ": " +
        //             (error.message ?: "cannot be emitted")
        //         null
        //     }
        // }
        val document = Profile.fromValueMap(
            release = release,
            route = RouteKind.resolve(RouteKind.normalize(route)),
            text = { path -> ProfileResolver.nativeText(resolved, path) },
            bool = { path -> ProfileResolver.nativeBool(resolved, path) },
            value = { path -> ProfileResolver.nativeValue(resolved, route, path) },
            /* M4(a): the step queue is an array of maps -> the raw accessor. */
            raw = { path -> resolved.getValueAt(path) },
            /* plugins = emissions, -- COMMENTED OUT (user ruling 2026-10-05) */
            plugins = emptyList(),
        )
        /* return NativeDocument(document, pluginErrors) -- COMMENTED OUT (user
         * ruling 2026-10-05); with no plugin emission there can be no plugin error. */
        return NativeDocument(document, emptyList(), declarationErrors)
    }

    // ---- resolution (migrated from ProfileConfiguration) ----

    /** Resolves through the currently selected builtin release (if any). */
    /** The runtime form only: every legacy caller keeps its exact behaviour. */
    private fun resolveCurrent(
        deviceRelease: String,
        pair: CpuPair,
        overrides: ValueMap?,
        includeImported: Boolean,
    ): ValueMap? = resolveCurrentResolved(deviceRelease, pair, overrides, includeImported)?.runtime

    /** The runtime form PLUS the merged declaration (B-ii). */
    private fun resolveCurrentResolved(
        deviceRelease: String,
        pair: CpuPair,
        overrides: ValueMap?,
        includeImported: Boolean,
    ): ResolvedProfile? {
        val profileRelease = activeBuiltinRelease() ?: deviceRelease
        return resolve(deviceRelease, profileRelease, pair, overrides, includeImported)
    }

    private fun resolve(
        deviceRelease: String,
        profileRelease: String,
        pair: CpuPair,
        overrides: ValueMap?,
        includeImported: Boolean,
    ): ResolvedProfile? = runCatching {
        val index = readIndex() ?: return@runCatching null
        LegacyProfileConverter.normalizeSchemaVersion(
            index["schema_version"], "index.conf",
        )
        val builtinEntry = findProfile(index["profiles"].asValueList(), profileRelease)
        /* (B-ii) the imported layer hands out BOTH forms: the runtime one every
         * consumer already used, and the declaration it had before normalization. */
        val importedEntry = if (includeImported) {
            userProfiles.loadEntryWithDeclaration(deviceRelease, activeUserProfile())
        } else {
            null
        }
        val imported = importedEntry?.runtime
        LegacyProfileConverter.convertValue(overrides)
        if (builtinEntry == null && imported == null) return@runCatching null
        val builtin = builtinEntry?.let { entry ->
            val path = (entry["file"] as? String).orEmpty()
            val assetPath = "$BuiltinDirectory/$path"
            val rawText = readAsset(assetPath)
            val parsed = HoconSupport.parseValue(rawText).asValueMap()
                ?: error("profile is not an object: " + assetPath)
            /* (B-ii) capture the DECLARATION before the normalization below
             * replaces the canonical layout with the runtime projection (which
             * has no `available`): same parse, no second read path (M5).
             * The RAW parse keeps the R3 wrapper `ghostlock { ... }`, so `available`
             * sits UNDER it - reading the top level returned null and this capture was
             * silently EMPTY in production (the runtime carrier path masked it, so no
             * test failed). Reuse the official unwrap; never hand-roll one. */
            /* CONVENTION (one shape, fixed here): `declaration` is a DOCUMENT that
             * CARRIES the `available` key - the shape every consumer expects
             * (ProfileLayout.declaredCombinations / AvailablePriority.selectedBackend
             * both read `["available"]`). HISTORY: it used to be the CONTENTS of
             * `available`, so those readers got null and the projection was silently
             * EMPTY - the UI combination tree had no rows (the A301SO / N=0 defect). */
            /* SELF-EXPLAINING EVIDENCE (one run names the layer): the capture must
             * either find `available` in the raw asset, or the asset genuinely has
             * none (the "none" class). Anything else - wrong path, unmet unwrap
             * precondition, parsed shape - is reported with all facts instead of
             * silently projecting an empty list (the N=0 defect). */
            val unwrapped = HoconSupport.unwrapProfileDocument(parsed).asValueMap()
            val availableValue = unwrapped?.get("available")
            /* Trigger on KEY PRESENCE after the unwrap, never on a substring of the
             * raw text: a comment mentioning `available` (the general assets carry
             * one) made the substring form fire on assets that declare nothing,
             * aborting the load and emptying invalidPaths (a false positive). The
             * substring stays in the MESSAGE as information only. */
            /* Trigger ONLY on a real contradiction: the key is present with a NON-NULL
             * value that is not a usable map. A missing key and a key whose value is
             * null are both LEGAL encodings of "this profile declares nothing" - firing
             * on them aborted the load and emptied invalidPaths (a false positive). */
            require(availableValue == null || availableValue is Map<*, *>) {
                "layer=builtin release=" + (parsed["release"] ?: path) +
                    " asset=" + assetPath +
                    " rawHasAvailableLiteral=" + rawText.contains("available") +
                    " topKeys=" + parsed.keys.sorted() +
                    " unwrappedKeys=" + (unwrapped?.keys?.sorted() ?: listOf("<not-a-map>"))
            }
            val declaration = valueMapOf(
                "available" to availableValue?.asValueMap()?.copyValue()?.asValueMap(),
            )
            /* R3: normalize the canonical owner-qualified layout (or a
             * legacy flat document) at parse time. */
            ProfileLayout.applyNormalize(parsed)
            LegacyProfileConverter.normalizeSchemaVersion(
                parsed["schema_version"], "$BuiltinDirectory/$path",
            )
            require(parsed["release"] == entry["release"]) {
                "profile index release mismatch"
            }
            BuiltinLayer(parsed, declaration)
        }
        val tuningExecution = readExecutionTuning()?.get("execution").asValueMap()
        val routePresets = ProfileConfig.Routes
            .mapNotNull { route -> readExecutionRoute(route)?.let { route to it } }
            .toMap()
        val runtime = ProfileMerger.resolveMerged(
            deviceRelease = deviceRelease,
            builtin = builtin?.runtime,
            imported = imported,
            overrides = overrides,
            tuningExecution = tuningExecution,
            pair = CpuPairView(pair.primary, pair.consumer),
            routePresets = routePresets,
        )
        /* One merge, two outputs (B-ii): the runtime profile above and the
         * DECLARATION it was projected from. The declaration comes from the
         * layers that own the parse, never from re-reading the runtime map (M5). */
        val declaration = ProfileMerger.resolveDeclarations(
            builtin?.declaration,
            importedEntry?.declaration,
            overrides?.get("available").asValueMap(),
        )
        ResolvedProfile(runtime, declaration)
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
        /* Same single convention as the write side: walk the dotted path. */
        fun read(root: ValueMap, path: String): Long? = root.getLongAt(path)
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
                    /* The top-level `execution` section is edited on the general
                     * page, so it stays out of the advanced tree. The refactor
                     * also nests per-backend tuning under `backend.<id>.execution`,
                     * which the advanced editor owns (Cve2026_43284Fields);
                     * skipping the key at any depth used to swallow those rows. */
                    if (key == "execution" && prefix.isEmpty()) continue
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
    private suspend fun persistSnapshot(release: String, pair: CpuPair) {
        runCatching {
            val resolved = resolveCurrentResolved(
                release, pair, readAdvancedOverride(release), includeImported = true,
            ) ?: return
            val exportView = resolved.runtime.copyValue().asValueMap() ?: return
            /* Renderer-side completeness: pull in the tuning of the selected
             * route, then drop the groups that are not used. */
            fillRouteExecutionDefaults(exportView)
            trimRouteTuning(exportView)
            File(filesDir, snapshotName(release))
                .writeText(HoconSupport.render(exportView), StandardCharsets.UTF_8)
            cache(release, buildNativeDocument(release, resolved.runtime, resolved.declaration).profile)
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
        private const val BuiltinDirectory = "profile"
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
