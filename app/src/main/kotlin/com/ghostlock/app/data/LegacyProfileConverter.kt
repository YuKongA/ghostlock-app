package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.profile.GHOSTLOCK_PROFILE_LEGACY_SCHEMA_VERSION
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import com.ghostlock.app.data.profile.GHOSTLOCK_PROFILE_SCHEMA_VERSION

/**
 * Converts remote/main-era offsets.json entries into the current layout.
 *
 * That era only shipped 6.x kernels with no Shizuku path and no 5.x geometry:
 * the extractor report carries `kernel_phys_load`, `pselect_waiter_shift`,
 * `compact_waiter`, `mm_struct_sz`, a `symbols` object keyed by `off_*`, a
 * `struct_fields` object keyed by `task_*`, plus `kimage_text_base`/`btf_size`
 * metadata. Credential templates came from the built-in profile, not the
 * report, so none are invented here. The conversion is idempotent and also
 * normalises the local transition formats (flat prefixes, `route` strings,
 * `fallback_to`). Multicast (a newer route) is intentionally unsupported here:
 * `remote/main`-era documents never selected it.
 */
private fun moveKey(
    source: ValueMap,
    target: ValueMap,
    from: String,
    to: String,
) {
    if (source.containsKey(from) && source[from] != null && !target.containsKey(to)) {
        target[to] = source[from]
    }
    source.remove(from)
}

internal object LegacyProfileConverter {
    /**
     * Canonicalises a profile document's `schema_version`.
     *
     * This object is the **single migration point**: the HOCON files, the
     * in-memory model and the GLKv3 wire all carry [GHOSTLOCK_PROFILE_SCHEMA_VERSION],
     * so a legacy [GHOSTLOCK_PROFILE_LEGACY_SCHEMA_VERSION] is accepted here and
     * normalized; any other value is rejected with the version actually seen.
     * [where] labels the diagnostic only.
     */
    fun normalizeSchemaVersion(version: Any?, where: String): Int {
        val seen = (version as? Number)?.toInt()
        return when (seen) {
            GHOSTLOCK_PROFILE_SCHEMA_VERSION -> seen
            /* Absent counts as legacy: documents written before the version was
             * mandatory are still our own files and are normalized here. */
            GHOSTLOCK_PROFILE_LEGACY_SCHEMA_VERSION, null -> {
                /* stderr instead of android.util.Log: the converter also runs in
                 * plain JVM unit tests, where Log is not mocked. */
                System.err.println(
                    "GhostLockProfile: profile_schema_version=$seen normalized to " +
                        "$GHOSTLOCK_PROFILE_SCHEMA_VERSION ($where)",
                )
                GHOSTLOCK_PROFILE_SCHEMA_VERSION
            }
            else -> error(
                "unsupported profile schema version $seen " +
                    "(expected $GHOSTLOCK_PROFILE_SCHEMA_VERSION)",
            )
        }
    }

    private val MetadataKeys = listOf("kimage_text_base", "btf_size", "kallsyms")

    /* Only keys the current schema knows are moved; upstream reports also
     * carry BTE-only fields (rt_mutex_waiter, cred_uid, seccomp_*) that stay
     * in place and are simply ignored by validation. */
    private val TaskStructFields = setOf(
        "prio", "normal_prio", "sched_task_group", "pi_lock", "pi_waiters",
        "pi_top_task", "pi_blocked_on", "pid", "tgid", "atomic_flags",
        "real_cred", "cred", "comm", "tasks", "seccomp",
    )
    private val CredFields = setOf(
        "copy_size", "usage_offset", "usage_value", "caps_offset", "caps_count",
        "caps_value", "ref_count", "ref0_offset", "ref1_offset", "ref2_offset",
        "ref3_offset", "ref0_image", "ref1_image", "ref2_image", "ref3_image",
    )
    private val OffsetFields = setOf(
        "init_task", "init_cred",
        "root_task_group", "selinux_enforcing", "selinux_blob_sizes",
        "security_hook_heads", "slide_nfulnl_logger", "slide_loggers_0_1",
        "slide_boot_id",
    )

    /* Per-route legacy layout codec. Adding a route = add a codec + register it
     * below; the branch/fallback moves stop being a `when` over route names. */
    private interface LegacyRouteCodec {
        fun fillBranch(entry: ValueMap, branch: ValueMap)
        fun fillFallback(entry: ValueMap, branch: ValueMap)
    }

    private object TcpRouteCodec : LegacyRouteCodec {
        override fun fillBranch(entry: ValueMap, branch: ValueMap) =
            moveKey(entry, branch, "compact_waiter", "compact_waiter")

        override fun fillFallback(entry: ValueMap, branch: ValueMap) =
            moveKey(entry, branch, "compact_waiter", "compact_waiter")
    }

    private object SelectRouteCodec : LegacyRouteCodec {
        override fun fillBranch(entry: ValueMap, branch: ValueMap) =
            moveKey(entry, branch, "pselect_waiter_shift", "waiter_shift")

        override fun fillFallback(entry: ValueMap, branch: ValueMap) =
            moveKey(entry, branch, "pselect_waiter_shift", "waiter_shift")
    }

    private val RouteCodecs: Map<String, LegacyRouteCodec> = mapOf(
        "tcp_zerocopy" to TcpRouteCodec,
        "select_stack" to SelectRouteCodec,
    )

    /* Values the bundled `credential-6x.conf` and `kernelsnitch-6x.conf` carry.
     * An imported extractor report has no `include` line and no such fields, so
     * the conversion seeds them here; the values must stay in sync with those
     * assets (BuiltinProfilesTest pins that). */
    private val CredDefaults6x = valueMapOf(
        "caps_offset" to 48L,
        "copy_size" to 136L,
        "usage_value" to 1L,
        "caps_count" to 5L,
        "caps_value" to -1L,
    )
    private val KernelsnitchDefaults6x = valueMapOf(
        "collisions" to 4L,
    )

    // ---- v2 binary document -> canonical v3 (HOCON refactor) ----

    /** The writer's hard-coded terminal id (binary.cpp:471 writes kTerminalRootChild). */
    private const val V2_WRITER_TERMINAL = 1

    /* The v2 header ids are resolved through the Kotlin catalogs and NEVER through
     * hand-written token literals (CombinationTokenHardcodeTest enforces that the
     * cross-end lists are read, not copied). */
    private fun v2Terminal(id: Int): FrontendKind? =
        FrontendKind.entries.firstOrNull { it.wire == id }

    private fun v2Backend(id: Int): BackendKind? =
        BackendKind.entries.firstOrNull { it.wire == id }

    private fun v2Route(id: Int): RouteKind? =
        RouteKind.entries.firstOrNull { it.wire == id.toUInt() }

    /** Root scalars: `common` carries these and NOTHING else in v3. */
    private val V2RootScalars = setOf("kernel_major", "kernel_minor", "safe_mode")

    /** Split keys, copied verbatim from the removed writer's owner_section_for(). */
    private val V2PlatformCredKeys = setOf(
        "usage_offset", "caps_offset", "ref_count",
        "ref0_offset", "ref1_offset", "ref2_offset", "ref3_offset",
    )
    private val V2PlatformOffsetKeys = setOf(
        "init_task", "init_cred", "empty_zero_page", "root_task_group",
        "selinux_enforcing", "selinux_blob_sizes", "security_hook_heads",
    )
    private val V2PlatformKernelKeys = setOf("kernel_phys_load", "kernel_phys_offset")

    /**
     * v2 binary document -> canonical v3 map (the second hop of the migration).
     *
     * Hop 1 re-owns the v2 section names exactly as the removed writer did
     * (`git show acaa0ac5^:src/core/profile/binary.cpp`, owner_section_for +
     * add_normalized): meta -> common, the key-aware cred/offset/kernel splits,
     * route.* and execution.* under the 43499 backend. Hop 2 folds that
     * pre-refactor shape onto the FINAL v3 shape: the kernel scalars move to the
     * profile root, platform.abi.* moves under backend.cve_2026_43499.abi.*, every
     * backend.* path must exist in the native-exported manifest, and the deleted
     * vr_guard surface is dropped with a diagnostic. Anything else is rejected
     * with its dotted path (there is NO v3 compatibility layer: it was never
     * released).
     *
     * Fail-closed additions with evidence:
     *  - the header ROUTE id is an invariant with the body - the writer emitted
     *    only the active route's section (binary.cpp:451-455) and never wrote Auto
     *    (:442) - so at most one route branch may appear and it must be the
     *    header's; Auto (43284 only) allows none;
     *  - the header terminal/backend are HARD-CODED constants in the writer
     *    (:471/472), so a mismatch with the body is DIAGNOSED, never fatal.
     *
     * Presence: v2 expresses it by key occurrence, so an absent key is never
     * materialised as 0. int targets use raw.toLong() (two's complement, and
     * bit-preserving for uint); bool requires 0/1.
     * TODO(width): check the native manifest's width column once it lands - today
     * only the wire TYPE is validated.
     */
    fun convertV2(bytes: ByteArray): ValueMap {
        val document = WireV2Reader.read(bytes)
        val root = ValueMap()
        val owners = ValueMap()
        val routeBranches = linkedSetOf<String>()
        var stepsToken: String? = null
        var stepsOwnerName: String? = null

        /** Nests one level per dotted segment (mutableChild treats its name as
         * ONE literal key, so a dotted owner path must be split here). */
        fun ownerMap(path: String): ValueMap {
            var node: ValueMap = owners
            for (part in path.removePrefix("backend.").split('.')) node = node.mutableChild(part)
            return node
        }

        fun record(path: String, value: Any?) {
            /* The owners map is attached at root["backend"], so the leading
             * "backend." is stripped here — otherwise the flatten path would
             * carry it twice. */
            val ownerName = path.substringBeforeLast('.', "").removePrefix("backend.")
            val ownerKey = path.substringAfterLast('.')
            if (ownerName.isEmpty()) {
                root[ownerKey] = value
            } else {
                ownerMap(ownerName)[ownerKey] = value
            }
            if (path.endsWith(".steps")) {
                stepsToken = value as? String
                stepsOwnerName = ownerName
            }
        }

        /** Hop 1: the writer's owner_section_for() plus the route/execution prefixes. */
        fun hop1(section: String, key: String): String = when {
            section == "meta" -> "common"
            section == "task_struct" -> "platform.abi.task_struct"
            section == "vr_guard" -> "countermeasure.vivo_vr_guard"
            section == "cred" -> if (key in V2PlatformCredKeys) {
                "platform.abi.cred"
            } else {
                "backend.cve_2026_43499.cred"
            }

            section == "offset" -> if (key in V2PlatformOffsetKeys) {
                "platform.abi.offset"
            } else {
                "backend.cve_2026_43499.offset"
            }

            section == "kernel" -> if (key in V2PlatformKernelKeys) {
                "platform.abi.kernel"
            } else {
                "backend.cve_2026_43499.kernel"
            }

            section.startsWith("route.") || section.startsWith("execution.") ->
                "backend.cve_2026_43499." + section

            else -> section
        }

        for (section in document.sections) {
            for (entry in section.entries) {
                val hop1Section = hop1(section.name, entry.key)
                val hop1Path = hop1Section + "." + entry.key
                if (hop1Section == "countermeasure.vivo_vr_guard" || hop1Section == "vr_guard") {
                    System.err.println(
                        "ghostlock: discarded: " + hop1Path +
                            " reason=removed-in-b55708a8/4a182217",
                    )
                    continue
                }
                val path = when {
                    hop1Section == "common" -> entry.key
                    hop1Section.startsWith("platform.abi.") ->
                        "backend.cve_2026_43499.abi." +
                            hop1Section.removePrefix("platform.abi.") + "." + entry.key

                    else -> hop1Path
                }
                if (hop1Section == "common" && entry.key !in V2RootScalars) {
                    throw IllegalArgumentException(hop1Path + ": deleted in the HOCON refactor")
                }
                if (path.startsWith("backend.cve_2026_43499.route.")) {
                    routeBranches += path.removePrefix("backend.cve_2026_43499.route.")
                        .substringBefore('.')
                }
                val wire = NativeProfileGlkv3Adapter.declaredWire(path)
                    ?: throw IllegalArgumentException(path + ": not declared in the v3 manifest")
                record(path, v2Value(wire, entry.raw, path))
            }
        }

        val routeToken = v2Route(document.routeId)?.token
        if (routeToken == null) {
            require(routeBranches.isEmpty()) {
                "v2: Auto route (43284) but the body declares " + routeBranches
            }
        } else {
            require(routeBranches.size <= 1 && routeBranches.all { it == routeToken }) {
                "v2: header route " + routeToken + " does not match the body route " + routeBranches
            }
        }
        /* Header terminal/backend are writer constants (binary.cpp:471/472). */
        val backendKind = requireNotNull(v2Backend(document.backendId)) {
            "v2: unknown backend id " + document.backendId
        }
        val backendToken = backendKind.token
        val stepsOwner = stepsOwnerName
        if (stepsToken != null && stepsOwner != null && stepsOwner != backendToken) {
            System.err.println(
                "ghostlock: diagnostic: v2 header backend " + backendToken +
                    " != body owner " + stepsOwnerName,
            )
        }
        if (document.terminalId != V2_WRITER_TERMINAL) {
            System.err.println(
                "ghostlock: diagnostic: v2 header terminal id " + document.terminalId +
                    " (the writer hard-coded " + V2_WRITER_TERMINAL + ")",
            )
        }
        /* v2 has NO string area, so the combination token could not ride the body:
         * the header (route + terminal) WAS the whole selection. Recover it by
         * asking the catalogue which spec matches that pair - no token is ever
         * spelled here, the catalogue is the single authority. */
        if (stepsToken == null) {
            val terminalKind = v2Terminal(document.terminalId)
            val routeKind = v2Route(document.routeId)
            val spec = if (routeKind != null && terminalKind != null) {
                CombinationCatalog.forBackend(backendKind).firstOrNull {
                    it.route == routeKind && it.terminal == terminalKind
                }
            } else {
                null
            }
            if (spec != null) {
                ownerMap(backendToken)["steps"] = spec.token
                stepsToken = spec.token
                stepsOwnerName = backendToken
            } else {
                System.err.println(
                    "ghostlock: diagnostic: v2 header carries no resolvable selection token (" +
                        backendToken + "/" + document.routeId + "/" + document.terminalId + ")",
                )
            }
        }
        root["schema_version"] = GHOSTLOCK_PROFILE_SCHEMA_VERSION
        root["release"] = document.release
        if (owners.isNotEmpty()) root["backend"] = owners
        return root
    }

    /**
     * Wire-type interpretation with the native manifest's WIDTH column as a hard
     * validation input: a raw that cannot fit the declared type is rejected
     * instead of being silently wrapped. The width is in BYTES (1/2/4/8) and is
     * already fail-closed in the adapter; null means width-less (str/array).
     */
    private fun v2Value(
        wire: NativeProfileGlkv3Adapter.WireType,
        raw: ULong,
        path: String,
    ): Any {
        val widthBytes = NativeProfileGlkv3Adapter.declaredWidth(path)
        return when (wire) {
            NativeProfileGlkv3Adapter.WireType.Bool -> {
                require(raw <= 1uL) { path + ": bool target holds " + raw }
                raw == 1uL
            }

            NativeProfileGlkv3Adapter.WireType.Int -> {
                checkSignedFits(raw, widthBytes, path)
                raw.toLong()
            }

            NativeProfileGlkv3Adapter.WireType.UInt -> {
                checkUnsignedFits(raw, widthBytes, path)
                raw.toLong()
            }

            else -> throw IllegalArgumentException(
                path + ": v2 has no " + wire.manifestName + " source",
            )
        }
    }

    /** uint: the raw must fit the declared byte width. */
    private fun checkUnsignedFits(raw: ULong, widthBytes: Int?, path: String) {
        if (widthBytes == null) return
        val bits = widthBytes * 8
        if (bits >= 64) return
        val max = (1uL shl bits) - 1uL
        require(raw <= max) {
            path + ": " + raw + " does not fit uint" + bits
        }
    }

    /** int: the two's-complement value must fit the declared byte width. */
    private fun checkSignedFits(raw: ULong, widthBytes: Int?, path: String) {
        if (widthBytes == null) return
        val bits = widthBytes * 8
        if (bits >= 64) return
        val value = raw.toLong()
        require(value in -(1L shl (bits - 1))..((1L shl (bits - 1)) - 1)) {
            path + ": " + value + " does not fit int" + bits
        }
    }

    fun convertValue(entry: ValueMap?): ValueMap? {
        if (entry == null) return null
        /* The bundled extractor still emits v1 reports, which carry no
         * schema_version; every such document is normalised and seeded. A v2
         * profile carries schema_version and is only normalised in place, so an
         * author's own edit keeps showing its missing fields. */
        val legacy = !entry.containsKey("schema_version")
        MetadataKeys.forEach(entry::remove)
        moveFlatNamespaces(entry)
        moveSymbolGroups(entry)
        moveKernelsnitch(entry)
        if (legacy && entry.containsKey("release")) {
            /* remote/main-era reports carried no kernel_major (they are 6.x
             * only); when a full legacy document does not state one, default to
             * 6. A bundled/edited HOCON profile states its own value and wins,
             * and a sparse override without a release is never seeded. */
            if (!entry.containsKey("kernel_major")) entry["kernel_major"] = 6L
        }
        moveRouteLayout(entry)
        moveFallback(entry)
        dropEmptyRouteBranches(entry)
        if (legacy) applySharedDefaults(entry)
        return entry
    }

    /**
     * Seeds the shared 6.x geometry a legacy report never carries. Only a full
     * document (one with a release) is seeded, and values already present win.
     */
    private fun applySharedDefaults(entry: ValueMap) {
        if (!entry.containsKey("release")) return
        if ((entry["kernel_major"] as? Number)?.toLong() != 6L) return
        fillMissing(entry, "cred", CredDefaults6x)
        fillMissing(entry, "kernelsnitch", KernelsnitchDefaults6x)
    }

    private fun fillMissing(entry: ValueMap, namespace: String, defaults: Map<String, Any?>) {
        val target = entry.mutableChild(namespace)
        for ((field, value) in defaults) {
            if (!target.containsKey(field)) target[field] = value
        }
    }

    /**
     * Earlier builds recorded a route switch as an empty branch object; that
     * carries no geometry and must not shadow the built-in route (its fields
     * would all read as missing). Current switches seed null-valued fields, so
     * they are never dropped here.
     */
    private fun dropEmptyRouteBranches(entry: ValueMap) {
        entry["route"].asValueMap()?.let { route ->
            route.keys.toList()
                .filter { key -> route[key].asValueMap()?.isEmpty() == true }
                .forEach(route::remove)
            if (route.isEmpty()) entry.remove("route")
        }
        entry["fallback"].asValueMap()?.get("route").asValueMap()?.let { route ->
            route.keys.toList()
                .filter { key -> route[key].asValueMap()?.isEmpty() == true }
                .forEach(route::remove)
            if (route.isEmpty()) entry["fallback"].asValueMap()?.remove("route")
        }
    }

    /** `symbols.off_x` -> `offset.x`, `struct_fields.task_x` -> `task_struct.x`. */
    private fun moveSymbolGroups(entry: ValueMap) {
        moveGroup(entry, "symbols", "offset", "off_", OffsetFields)
        moveGroup(entry, "struct_fields", "task_struct", "task_", TaskStructFields)
        /* Local transition builds also stored credential/tuning keys here. */
        moveGroup(entry, "struct_fields", "cred", "cred_", CredFields)
        val structFields = entry["struct_fields"].asValueMap() ?: return
        val snitch = entry.mutableChild("kernelsnitch")
        moveKey(structFields, snitch, "kernelsnitch_collisions", "collisions")
        moveKey(structFields, snitch, "mm_struct_sz", "mm_struct_sz")
        if (snitch.isEmpty()) entry.remove("kernelsnitch")
        if (structFields.isEmpty()) entry.remove("struct_fields")
    }

    private fun moveGroup(
        entry: ValueMap,
        groupName: String,
        namespace: String,
        prefix: String,
        allowed: Set<String>,
    ) {
        val group = entry[groupName].asValueMap() ?: return
        val matching = group.keys
            .filter { it.startsWith(prefix) && it.removePrefix(prefix) in allowed }
        if (matching.isEmpty()) return
        val target = entry.mutableChild(namespace)
        for (key in matching) {
            val field = key.removePrefix(prefix)
            if (!target.containsKey(field)) target[field] = group[key]
            group.remove(key)
        }
        if (group.isEmpty()) entry.remove(groupName)
    }

    private fun moveFlatNamespaces(entry: ValueMap) {
        val namespaces = listOf(
            Triple("task_struct", "task_", TaskStructFields),
            Triple("cred", "cred_", CredFields),
            Triple("offset", "off_", OffsetFields),
        )
        for ((namespace, flatPrefix, allowed) in namespaces) {
            val keys = entry.keys
                .filter { it.startsWith(flatPrefix) && it.removePrefix(flatPrefix) in allowed }
            if (keys.isEmpty()) continue
            val nested = entry.mutableChild(namespace)
            for (key in keys) {
                val field = key.removePrefix(flatPrefix)
                if (!nested.containsKey(field)) nested[field] = entry[key]
                entry.remove(key)
            }
        }
        /* Namespace spellings from the transition builds. */
        for ((legacy, current) in listOf("task" to "task_struct", "off" to "offset")) {
            val legacyObject = entry[legacy].asValueMap() ?: continue
            val target = entry.mutableChild(current)
            legacyObject.forEach { (field, value) ->
                if (!target.containsKey(field)) target[field] = value
            }
            entry.remove(legacy)
        }
    }

    private fun moveKernelsnitch(entry: ValueMap) {
        val hasFlat = entry.containsKey("kernelsnitch_collisions") || entry.containsKey("mm_struct_sz")
        if (!hasFlat) return
        val nested = entry.mutableChild("kernelsnitch")
        moveKey(entry, nested, "kernelsnitch_collisions", "collisions")
        moveKey(entry, nested, "mm_struct_sz", "mm_struct_sz")
        if (nested.isEmpty()) entry.remove("kernelsnitch")
    }

    private fun moveRouteLayout(entry: ValueMap) {
        fun fillBranch(name: String, branch: ValueMap) {
            RouteCodecs[name]?.fillBranch(entry, branch)
        }

        when (val route = entry["route"]) {
            is String -> {
                val branch = valueMapOf()
                fillBranch(route, branch)
                entry["route"] = valueMapOf(route to branch)
            }

            is Map<*, *> -> Unit

            else -> if (entry.containsKey("release")) {
                /* Remote/main reports predate the route field: infer 6.x-only
                 * geometry (compact waiter -> tcp, otherwise the select path).
                 * Sparse override fragments carry no release and are never
                 * inferred, otherwise an empty override would inject a route. */
                val inferred = inferRoute(entry)
                val branch = valueMapOf()
                fillBranch(inferred, branch)
                entry["route"] = valueMapOf(inferred to branch)
            }
        }
        /* The flat keys stay for moveFallback: a tcp route keeps its pselect
         * shift as an explicit select_stack fallback. It removes them. */
    }

    private fun moveFallback(entry: ValueMap) {
        val fallbackValue = entry["fallback_to"]
        if (fallbackValue is String) {
            entry.remove("fallback_to")
            val fallback = entry["fallback"].asValueMap() ?: valueMapOf()
            fallback["to"] = fallbackValue
            if (fallbackValue != "none") {
                val branch = fallback["route"].asValueMap()?.get(fallbackValue).asValueMap()
                    ?: valueMapOf()
                RouteCodecs[fallbackValue]?.fillFallback(entry, branch)
                if (branch.isNotEmpty()) {
                    fallback["route"] = valueMapOf(fallbackValue to branch)
                }
            }
            entry["fallback"] = fallback
            entry.remove("compact_waiter")
            entry.remove("pselect_waiter_shift")
            return
        }
        if (entry["fallback"].asValueMap() != null || entry["route"].asValueMap() == null) {
            entry.remove("compact_waiter")
            entry.remove("pselect_waiter_shift")
            return
        }
        if (!entry.containsKey("release")) {
            /* Sparse override fragments must not gain an inferred fallback:
             * it would overwrite the choice kept in the offsets entry. */
            entry.remove("compact_waiter")
            entry.remove("pselect_waiter_shift")
            return
        }
        /* Remote/main had no fallback field; the tcp path could still fall
         * back whenever a pselect shift was present, so keep that capability
         * as an explicit declaration. */
        val routeName = entry["route"].asValueMap()?.keys?.firstOrNull()
        val pselect = entry["pselect_waiter_shift"]
        if (routeName == "tcp_zerocopy" && pselect != null) {
            entry["fallback"] = valueMapOf(
                "to" to "select_stack",
                "route" to valueMapOf("select_stack" to valueMapOf("waiter_shift" to pselect)),
            )
        } else {
            entry["fallback"] = valueMapOf("to" to "none")
        }
        entry.remove("compact_waiter")
        entry.remove("pselect_waiter_shift")
    }

    private fun inferRoute(entry: ValueMap): String {
        if (((entry["compact_waiter"] as? Number)?.toLong() ?: 0L) != 0L) return "tcp_zerocopy"
        return "select_stack"
    }
}
