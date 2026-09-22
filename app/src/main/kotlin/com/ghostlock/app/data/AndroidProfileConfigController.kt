package com.ghostlock.app.data

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import androidx.core.content.edit
import com.ghostlock.app.domain.model.CpuPair
import com.ghostlock.app.domain.model.ExecutionFieldValue
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
 * route/fallback and advanced) persists as a sparse override in preferences,
 * so stored user documents are never rewritten.
 */
internal class AndroidProfileConfigController(
    context: Context,
    private val filesDir: File,
    private val userProfiles: UserProfileStore,
    private val preferences: SharedPreferences,
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
        val fallbackTo = fallbackTargetOf(full)
        val invalidPaths = validateProfileFields(full, route, fallbackTo)
        /* Invalid fields missing from the resolved document still get a row,
         * otherwise the run stays blocked with no red field to fix. */
        materializeInvalidPaths(full, invalidPaths)
        val roots = buildTree(full, "", baseline, advanced)
        cache(deviceRelease, buildNativeDocument(deviceRelease, full))
        return ProfileConfig(
            release = deviceRelease,
            hasProfile = true,
            roots = roots,
            general = generalFields(full, baseline),
            route = route,
            fallbackTo = fallbackTo,
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
        fallbackTo: String?,
    ): Set<String> {
        val invalid = mutableSetOf<String>()
        fun value(path: String): Long? = profile.getLongAt(path)
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
        val fallbackPrefix = fallbackTo?.takeIf { it != "none" }
            ?.let { "fallback.route.$it" }
        when (route) {
            "tcp_zerocopy" -> {
                val compact = value("$routePrefix.compact_waiter")
                if (compact == null || compact == 0L) {
                    invalid += "$routePrefix.compact_waiter"
                }
            }

            "select_stack" -> {
                if (value("$routePrefix.waiter_shift") == null) {
                    invalid += "$routePrefix.waiter_shift"
                }
            }

            "multicast_waiter" -> {
                for (field in RouteMulticastFields) {
                    val current = value("$routePrefix.$field")
                    if (current == null || current == 0L) invalid += "$routePrefix.$field"
                }
                val compact = value("$routePrefix.compact_waiter")
                if (compact == null || compact == 0L) {
                    invalid += "$routePrefix.compact_waiter"
                }
                requireNonZero(
                    "offset.mcast_fake_bss",
                    "kernelsnitch.mm_struct_sz",
                    "offset.empty_zero_page",
                    "cred.ref_count",
                )
                if (copySize != null && copySize < 0xa0L) invalid += "cred.copy_size"
                val waiterOff = value("$routePrefix.waiter_off")
                if (waiterOff == null || waiterOff <= 0L) {
                    invalid += "$routePrefix.waiter_off"
                }
                val bufferSize = value("$routePrefix.buffer_size")
                val lockOffset = value("$routePrefix.lock_offset")
                if (waiterOff != null && waiterOff > 0L &&
                    bufferSize != null && lockOffset != null &&
                    waiterOff + lockOffset + SizeofU64 > bufferSize
                ) {
                    invalid += "$routePrefix.waiter_off"
                    invalid += "$routePrefix.lock_offset"
                    invalid += "$routePrefix.buffer_size"
                }
            }
        }
        if (fallbackTo != null && fallbackTo != "none" && fallbackTo !in ProfileConfig.Routes) {
            invalid += "fallback.to"
        } else if (fallbackPrefix != null) {
            when (fallbackTo) {
                "tcp_zerocopy" -> {
                    val compact = value("$fallbackPrefix.compact_waiter")
                    if (compact == null || compact == 0L) {
                        invalid += "$fallbackPrefix.compact_waiter"
                    }
                }

                "select_stack" -> {
                    if (value("$fallbackPrefix.waiter_shift") == null) {
                        invalid += "$fallbackPrefix.waiter_shift"
                    }
                }

                "multicast_waiter" -> {
                    for (field in RouteMulticastFields) {
                        val current = value("$fallbackPrefix.$field")
                        if (current == null || current == 0L) invalid += "$fallbackPrefix.$field"
                    }
                }
            }
        }
        return invalid
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

    private fun fallbackTargetOf(profile: ValueMap): String? {
        profile["fallback"].asValueMap()?.let { fallback ->
            val to = fallback["to"] as? String ?: ""
            if (to.isNotEmpty() && to != "null") return to
        }
        /* Legacy flat spelling from transition builds. */
        return (profile["fallback_to"] as? String)
            ?.takeIf { it.isNotEmpty() && it != "null" }
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

    override suspend fun updateFallback(
        release: String,
        pair: CpuPair,
        fallbackTo: String?,
    ): ProfileConfig {
        val override = readAdvancedOverride(release)
        if (fallbackTo.isNullOrBlank()) {
            override.remove("fallback")
        } else {
            val fallback = override["fallback"].asValueMap() ?: valueMapOf()
            fallback["to"] = fallbackTo
            if (fallbackTo in ProfileConfig.Routes) {
                val branch = fallback["route"].asValueMap()?.get(fallbackTo).asValueMap()
                    ?: routeBranchTemplate(fallbackTo)
                fallback["route"] = valueMapOf(fallbackTo to branch)
            } else {
                fallback.remove("route")
            }
            override["fallback"] = fallback
            pruneOverrideBranches(override, "fallback", fallbackTo.takeIf { it in ProfileConfig.Routes })
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
        values: Map<String, Long>,
    ): ProfileConfig {
        /* Rebuild the sparse override from scratch: only values that differ from
         * the baseline survive, so untouched fields (including stale entries
         * from older builds) can never stay highlighted. */
        val baseline = resolveCurrent(release, pair, null, includeImported = true)
        if (baseline != null) {
            val rebuilt = valueMapOf()
            for ((path, value) in values) {
                if (path.isEmpty() || path == "release" ||
                    path.startsWith("execution.selected_cpus")
                ) {
                    continue
                }
                if (value != baseline.getLongAt(path)) rebuilt.setValueAt(path, value)
            }
            /* The advanced editor carries neither the fallback choice nor the
             * selected CPUs (and may drop a route branch the baseline already
             * matches); keep all three. */
            val current = readAdvancedOverride(release)
            current["route"].asValueMap()?.let { route -> rebuilt.putIfAbsent("route", route) }
            current["execution"].asValueMap()?.get("selected_cpus")?.let { cpus ->
                rebuilt.mutableChild("execution")["selected_cpus"] = cpus
            }
            current["fallback"].asValueMap()?.let { fallback ->
                val rebuiltFallback = rebuilt["fallback"].asValueMap()
                if (rebuiltFallback == null) {
                    rebuilt["fallback"] = fallback
                } else {
                    fallback["to"]?.let { rebuiltFallback["to"] = it }
                }
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
        val output = resolver.openOutputStream(Uri.parse(documentUri), "wt") ?: return false
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
        view["schema_version"] = 1
        view["release"] = release
        /* The CPU choice follows the device pair, it must not be frozen here. */
        view["execution"].asValueMap()?.remove("selected_cpus")
        fillRouteExecutionDefaults(view)
        trimRouteTuning(view)
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

    override fun nativeDocument(config: ProfileConfig): ByteArray? = synchronized(lock) {
        if (cachedRelease == config.release) cachedProfile?.toBinary() else null
    }

    /** Builds the single resolved authority native consumes at run time. */
    private fun buildNativeDocument(release: String, profile: ValueMap): Profile? {
        val route = routeNameOf(profile)
        val fallbackTo = fallbackTargetOf(profile)
        return Profile.fromValueMap(
            release = release,
            route = RouteKind.fromToken(route),
            fallbackTo = RouteKind.fromToken(fallbackTo),
        ) { path -> nativeValue(profile, route, fallbackTo, path) }
    }

    /** Maps canonical native field names onto the declared route branches. */
    private fun nativeValue(
        profile: ValueMap,
        route: String?,
        fallbackTo: String?,
        path: String,
    ): Long? {
        val branchField = when (path) {
            "compact_waiter" -> "compact_waiter"
            "pselect_waiter_shift" -> "waiter_shift"
            else -> null
        }
        if (branchField != null) {
            route?.let { name ->
                profile.getLongAt("route.$name.$branchField")?.let { return it }
            }
            if (fallbackTo != null && fallbackTo != "none") {
                profile.getLongAt("fallback.route.$fallbackTo.$branchField")?.let { return it }
            }
        }
        if (path.startsWith("mcast.")) {
            val field = path.removePrefix("mcast.")
            route?.let { name ->
                profile.getLongAt("route.$name.$field")?.let { return it }
            }
            if (fallbackTo != null && fallbackTo != "none") {
                profile.getLongAt("fallback.route.$fallbackTo.$field")?.let { return it }
            }
            profile.getLongAt("mcast.$field")?.let { return it }
        }
        return profile.getLongAt(path)
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
        require((index["schema_version"] as? Number)?.toInt() == 1) { "unsupported profile schema" }
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
                    require((it["schema_version"] as? Number)?.toInt() == 1) { "unsupported profile schema" }
                    require(it["release"] == entry["release"]) {
                        "profile index release mismatch"
                    }
                }
        }
        /* Base layer: shared execution-tuning.conf values; every profile
         * includes the same file, so the values coincide. Imported offsets and
         * overrides still layer on top. */
        val tuning = readExecutionTuning()
        val defaults = valueMapOf("release" to deviceRelease).apply {
            tuning?.get("execution")?.let { put("execution", it) }
        }
        val resolved = mergeSource(
            mergeSource(
                if (builtin == null) defaults else deepMergeValues(defaults, builtin),
                if (includeImported) imported else null,
            ),
            overrides,
        )
        resolved["schema_version"] = 1
        /* A manually chosen builtin still reports the device release so the
         * native release gate and the exported document stay coherent. */
        resolved["release"] = deviceRelease
        val selectedOverride = imported?.get("execution").asValueMap()?.get("selected_cpus").asValueMap()
            ?: overrides?.get("execution").asValueMap()?.get("selected_cpus").asValueMap()
        applySelectedCpus(resolved, pair, selectedOverride)
        fillRouteExecutionDefaults(resolved)
        validateResolved(resolved, deviceRelease)
        resolved
    }.getOrNull()

    /**
     * Native decodes one complete `execution.routes` object, while profiles only
     * include the route they use (plus their fallback). Missing groups are
     * filled from the shared `execution-<route>.conf` files.
     */
    private fun fillRouteExecutionDefaults(profile: ValueMap) {
        val execution = profile["execution"].asValueMap() ?: return
        val routes = execution.mutableChild("routes")
        for (route in ProfileConfig.Routes) {
            if (routes.containsKey(route)) continue
            val defaults = readExecutionRoute(route) ?: continue
            routes[route] = defaults
        }
    }

    private fun readExecutionRoute(route: String): ValueMap? = runCatching {
        HoconSupport.parseValue(
            readAsset("$BuiltinDirectory/execution-${route.replace('_', '-')}.conf"),
        ).asValueMap()?.get("execution").asValueMap()?.get("routes").asValueMap()?.get(route).asValueMap()
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
        HoconSupport.parseValue(readAsset("$BuiltinDirectory/execution-tuning.conf")).asValueMap()
    }.getOrNull()

    private fun applySelectedCpus(
        resolved: ValueMap,
        pair: CpuPair,
        selectedOverride: ValueMap?,
    ) {
        val execution = resolved.mutableChild("execution")
        if (selectedOverride == null) {
            execution["selected_cpus"] = valueMapOf(
                "main" to pair.primary.toLong(),
                "consumer" to pair.consumer.toLong(),
            )
        } else {
            execution["selected_cpus"] = selectedOverride
        }
    }

    /**
     * Route objects hold exactly one branch. An incoming branch that differs
     * from the base replaces it (the old geometry belongs to the old route),
     * while the same branch merges field-wise.
     */
    private fun mergeRouteObjects(
        base: ValueMap?,
        incoming: Map<*, *>?,
    ): ValueMap? {
        if (incoming == null) return base
        val incomingBranch = incoming.keys.filterIsInstance<String>()
            .firstOrNull { it in ProfileConfig.Routes } ?: return base
        val incomingBody = incoming[incomingBranch].asValueMap() ?: valueMapOf()
        val baseBranch = base?.keys?.toList()
            ?.firstOrNull { it in ProfileConfig.Routes }
        return when {
            baseBranch == incomingBranch -> valueMapOf(
                incomingBranch to deepMergeValues(
                    base[incomingBranch].asValueMap() ?: valueMapOf(),
                    incomingBody,
                ),
            )

            /* A branch different from the base carries the new choice: the old
             * geometry belongs to the old route. */
            else -> valueMapOf(incomingBranch to incomingBody)
        }
    }

    /** deepMerge plus branch-replacement semantics for route/fallback.route. */
    private fun mergeSource(
        base: ValueMap,
        incoming: ValueMap?,
    ): ValueMap {
        /* Snapshot the branches first: deepMergeValues mutates its base argument,
         * so a later read would already contain the incoming branch. */
        val baseRoute = base["route"].asValueMap()?.copyValue().asValueMap()
        val baseFallbackRoute = base["fallback"].asValueMap()
            ?.get("route").asValueMap()?.copyValue().asValueMap()
        val merged = deepMergeValues(base, incoming)
        if (incoming == null) return merged
        incoming["route"].asValueMap()?.let { route ->
            mergeRouteObjects(baseRoute, route)?.let { merged["route"] = it }
        }
        incoming["fallback"].asValueMap()?.get("route").asValueMap()?.let { fallbackRoute ->
            val fallback = merged["fallback"].asValueMap() ?: return@let
            mergeRouteObjects(baseFallbackRoute, fallbackRoute)?.let { fallback["route"] = it }
        }
        return merged
    }

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
        RouteBranchFields[route].orEmpty().forEach { template[it] = null }
        return template
    }

    /**
     * After a route/fallback switch only the selected branch survives in the
     * advanced override; edits of the previous branch would otherwise pull the
     * choice back or shadow the new branch's fields.
     */
    private fun pruneOverrideBranches(entry: ValueMap, key: String, keep: String?) {
        val container = if (key == "fallback") {
            entry["fallback"].asValueMap()?.get("route").asValueMap()
        } else {
            entry[key].asValueMap()
        }
        if (container != null) {
            container.keys.toList()
                .filter { it != keep }
                .forEach(container::remove)
            if (container.isEmpty()) {
                if (key == "fallback") {
                    entry["fallback"].asValueMap()?.remove("route")
                } else {
                    entry.remove(key)
                }
            }
        }
        if (key == "fallback") {
            entry["fallback"].asValueMap()?.let { fallback ->
                if (fallback.isEmpty()) entry.remove("fallback")
            }
        }
    }

    private fun validateResolved(profile: ValueMap, release: String) {
        require(profile["release"] == release) { "profile release mismatch" }
        require((profile["kernel_major"] as? Number)?.toInt() in 5..6) { "invalid kernel_major" }
        require(profile.containsKey("execution")) { "missing execution tuning" }
    }

    // ---- controller model helpers ----

    private fun generalFields(
        profile: ValueMap,
        baseline: ValueMap,
    ): List<ExecutionFieldValue> {
        fun read(root: ValueMap, path: String): Long? = if (path.startsWith("execution.")) {
            root["execution"].asValueMap()?.getLongAt(path.removePrefix("execution."))
        } else {
            root.getLongAt(path)
        }
        return ProfileConfig.GeneralPaths.map { path ->
            val value = read(profile, path) ?: 0L
            ExecutionFieldValue(
                path = path,
                value = value,
                overridden = read(baseline, path)?.let { baselineValue -> value != baselineValue } == true,
            )
        }
    }

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
                    if (path == "execution.selected_cpus" || path == "execution.routes") continue
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

                value is Number -> if (path != "schema_version" && path != "release" &&
                    path != "recommend_shizuku"
                ) {
                    val overrideValue = override.getLongAt(path)
                    leaves += ProfileFieldNode(
                        path = path,
                        name = key,
                        value = value.toLong(),
                        overridden = overrideValue != null &&
                            overrideValue != baseline.getLongAt(path),
                    )
                }

                value == null -> if (path != "schema_version" && path != "release" &&
                    path != "recommend_shizuku"
                ) {
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

    /** Sparse overrides for [release]: general, route/fallback and advanced. */
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
             * route and its fallback, then drop the groups that are not used. */
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

    /** The exported document keeps only the selected route's tuning (+ fallback). */
    private fun trimRouteTuning(profile: ValueMap) {
        val routes = profile["execution"].asValueMap()?.get("routes").asValueMap() ?: return
        val keep = buildSet {
            routeNameOf(profile)?.let(::add)
            fallbackTargetOf(profile)?.takeIf { it != "none" }?.let(::add)
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
        /** Fields each route branch carries, used to seed a switched-to route. */
        private val RouteBranchFields = mapOf(
            "tcp_zerocopy" to listOf("compact_waiter"),
            "select_stack" to listOf("waiter_shift"),
            "multicast_waiter" to listOf(
                "waiter_off", "buffer_size", "task_offset", "lock_offset",
                "fake_lock_offset", "fake_task_offset", "lock_slots_offset",
                "lock_slot_count", "lock_slot_stride", "compact_waiter",
            ),
        )
        private val RouteMulticastFields = listOf(
            "buffer_size", "task_offset", "lock_offset", "fake_lock_offset",
            "fake_task_offset", "lock_slots_offset", "lock_slot_count", "lock_slot_stride",
        )
        private const val SizeofU32 = 4L
        private const val SizeofU64 = 8L
        private const val PrefBuiltinRelease = "debug_builtin_release"
        private const val PrefActiveUserProfile = "active_user_profile"
        const val PrefDebugProfileOverrides = "debug_profile_overrides"
    }
}
