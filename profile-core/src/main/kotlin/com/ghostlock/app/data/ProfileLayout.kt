package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.data.plugin.PluginPaths
import com.ghostlock.app.data.route.RouteKind

/**
 * S4 R3 canonical HOCON layout and parse-time normalization.
 *
 * Bundled profiles live in the canonical owner-qualified layout (Wrapper root:
 * ghostlock { schema_version release selection common platform.abi backend.<id>
 * countermeasure }). The parser accepts both that layout and the legacy flat
 * one; every document is normalized at parse time into a single owner-qualified
 * canonical form and then projected onto the in-memory logical model the
 * resolver/UI already consume (toRuntime). Unrecognized keys fail closed with
 * their dotted path instead of being dropped.
 *
 * Non-identity alias exceptions (legacy -> canonical):
 *  - backend.steps / selection.steps (legacy step id) -> the owning
 *    backend.<id>.steps combination token; selection.steps is cancelled from
 *    the canonical shape (S4 R6b) and only accepted+migrated as legacy input;
 *  - kernel_major -> common.kernel_major;
 *  - fallback.to / fallback.route.<kind>.* / common.fallback_route are
 *    recognized-but-ignored legacy keys (R6a removed route fallback from the
 *    wire); they never fail as unrecognized and never reach the runtime model;
 *  - kernelsnitch.collisions -> backend.cve_2026_43499.kernel.kernelsnitch_collisions;
 *  - kernelsnitch.mm_struct_sz -> backend.cve_2026_43499.kernel.mm_struct_sz;
 *  - cred is split: platform layouts -> platform.abi.cred, the 43499 template
 *    -> backend.cve_2026_43499.cred;
 *  - offset is split: platform symbols -> platform.abi.offset, slide/vr anchors
 *    -> backend.cve_2026_43499.offset;
 *  - kernel_phys_load|offset -> platform.abi.kernel.*;
 *  - task_struct -> platform.abi.task_struct;
 *  - route.<kind>.<field> -> backend.cve_2026_43499.route.<kind>.<field>;
 *  - route.<tcp_zerocopy|multicast_waiter>.compact_waiter -> the shared
 *    backend.cve_2026_43499.kernel.compact_waiter wire flag;
 *    route.select_stack.compact_waiter keeps its route field and sets the flag;
 *  - execution.* -> backend.cve_2026_43499.execution.*;
 *  - recommend_vr_guard -> common.vr_guard;
 *  - vr_guard.tracepoint_funcs -> countermeasure.vivo_vr_guard.tracepoint_funcs;
 *  - new-only selection.backend / selection.terminal have no legacy source:
 *    full legacy documents get the 43499 / root_child defaults, fragments (no
 *    release) carry no selection at all.
 */
object ProfileLayout {
    /** Canonical root object wrapping a device profile. */
    const val Wrapper: String = "ghostlock"

    private val BackendTokens: Set<String> = linkedSetOf(
        BackendKind.Cve2026_43499.token,
        BackendKind.Cve2026_43284.token,
    )

    private val RouteTokens: Set<String> = RouteKind.entries.mapTo(linkedSetOf()) { it.token }
    private val TerminalTokens: Set<String> = FrontendKind.entries.mapTo(linkedSetOf()) { it.token }
    /**
     * Canonical selection keys. `steps` is deliberately absent: the token
     * lives at backend.<id>.steps (S4 R6b). It is still accepted as a legacy
     * step id and migrated before validation.
     */
    private val LegacySelectionKeys = setOf("backend", "steps", "terminal")
    private val CommonKeys = setOf("kernel_major", "fallback_route", "safe_mode", "vr_guard")
    private val AbiKeys = setOf("kernel", "task_struct", "cred", "offset")
    private val PlatformKernelKeys = setOf("kernel_phys_load", "kernel_phys_offset")
    private val BackendKernelKeys = setOf(
        "compact_waiter", "kernelsnitch_collisions", "mm_struct_sz",
    )
    private val Backend43499Keys = setOf("steps", "kernel", "cred", "offset", "route", "execution")
    private val CountermeasureKeys = setOf("vivo_vr_guard")

    /**
     * P1 plugin section keys. This is SHAPE validation only: the layout knows
     * nothing about the installed registry or the probe descriptors, so it can
     * only check the id shape, the key whitelist and that parameter names are
     * non-blank text. Registry semantics (unknown plugin id, parameter outside
     * the descriptor, type mismatch) belong to the App layer
     * ([com.ghostlock.app.data.plugin.PluginConfigValidator] and the emission),
     * never here.
     */
    private val PluginKeys = setOf("enabled", "stage", "module_path", "module_hash", "params", "extract")
    private val PluginGroupKeys = listOf("params", "extract")
    private val VivoGuardKeys = setOf("tracepoint_funcs")

    private val TaskFields = linkedSetOf(
        "prio", "normal_prio", "sched_task_group", "pi_lock", "pi_waiters", "pi_top_task",
        "pi_blocked_on", "pid", "tgid", "atomic_flags", "real_cred", "cred", "comm", "tasks",
        "seccomp",
    )
    private val CredFields = linkedSetOf(
        "copy_size", "usage_offset", "usage_value", "caps_offset", "caps_count", "caps_value",
        "ref_count", "ref0_offset", "ref1_offset", "ref2_offset", "ref3_offset",
        "ref0_image", "ref1_image", "ref2_image", "ref3_image",
    )
    private val OffsetFields = linkedSetOf(
        "init_task", "init_cred", "empty_zero_page", "root_task_group", "selinux_enforcing",
        "selinux_blob_sizes", "security_hook_heads", "slide_nfulnl_logger", "slide_loggers_0_1",
        "slide_boot_id", "vr_sys_exit_tp",
    )
    private val KernelsnitchFields = setOf("collisions", "mm_struct_sz")

    private val PlatformCredKeys = setOf(
        "usage_offset", "caps_offset", "ref_count",
        "ref0_offset", "ref1_offset", "ref2_offset", "ref3_offset",
    )
    private val PlatformOffsetKeys = setOf(
        "init_task", "init_cred", "empty_zero_page", "root_task_group",
        "selinux_enforcing", "selinux_blob_sizes", "security_hook_heads",
    )

    private val KnownLegacyTopLevel = setOf(
        "schema_version", "release", "kernel_major", "backend",
        "kernel_phys_load", "kernel_phys_offset", "route", "fallback", "fallback_to",
        "kernelsnitch", "task_struct", "cred", "offset", "execution",
        "recommend_vr_guard", "vr_guard", "safe_mode",
        "symbols", "struct_fields", "kimage_text_base", "btf_size", "kallsyms",
    )

    /** True when [raw] is already in the canonical owner-qualified layout. */
    fun isCanonical(raw: Map<*, *>): Boolean {
        if (raw.containsKey(Wrapper)) return true
        if (raw.keys.any { it.toString() in setOf("selection", "common", "platform", "countermeasure") }) {
            return true
        }
        val backend = raw["backend"]
        return backend is Map<*, *> && backend.keys.any { it.toString() in BackendTokens }
    }

    /** Normalizes [raw] (either layout) to the canonical owner-qualified map. */
    fun canonicalize(raw: ValueMap): ValueMap =
        if (isCanonical(raw)) canonicalizeCanonical(raw) else canonicalizeLegacy(raw)

    /** Legacy-flat compatibility projection of a canonical map. */
    fun toRuntime(canonical: ValueMap): ValueMap = buildRuntime(canonical)

    /** Canonical owner-qualified map -> in-memory logical model. */
    fun normalize(raw: ValueMap): ValueMap = toRuntime(canonicalize(raw))

    /** In-place [normalize], for callers that already hold the parsed map. */
    fun applyNormalize(raw: ValueMap) {
        val normalized = normalize(raw)
        raw.clear()
        raw.putAll(normalized)
    }

    // ---- canonical validation ----

    private fun canonicalizeCanonical(raw: ValueMap): ValueMap {
        val inner = migrateLegacySelection(raw[Wrapper].asValueMap() ?: raw)
        val out = ValueMap()
        for (key in inner.keys) {
            val value = inner[key]
            when (key) {
                "schema_version", "release" -> out[key] = value
                "selection" -> out[key] = requireObject("selection", value) { validateSelection(it) }
                "common" -> out[key] = requireObject("common", value) { validateCommon(it) }
                "platform" -> out[key] = requireObject("platform", value) { validatePlatform(it) }
                "backend" -> out[key] = requireObject("backend", value) { validateBackend(it) }
                "countermeasure" -> out[key] = requireObject("countermeasure", value) {
                    validateCountermeasure(it)
                }

                "plugin" -> out[key] = requireObject("plugin", value) { validatePlugins(it) }
                else -> fail(key)
            }
        }
        return out
    }

    /**
     * S4 R6b legacy migration: a canonical document may still carry the old
     * `selection.steps` step id. It is read and folded into the owning
     * `backend.<id>.steps` as the equivalent combination token (using the route
     * the profile declares), then removed from `selection`. The resulting token
     * is validated by [validateBackend], which fails closed with the token text.
     */
    private fun migrateLegacySelection(inner: ValueMap): ValueMap {
        val selection = inner["selection"].asValueMap() ?: return inner
        val legacySteps = selection["steps"] as? String ?: return inner
        val backendSection = inner["backend"].asValueMap() ?: return inner
        val backendKind = BackendKind.resolve(
            BackendKind.normalize(selection["backend"] as? String),
        ) ?: BackendKind.Default
        val combination = CombinationCatalog.fromLegacySteps(
            backendKind, legacySteps, declaredRoute(backendSection, backendKind),
        ) ?: throw IllegalArgumentException(
            "selection.steps is not a known step set for ${backendKind.token}: $legacySteps"
        )
        val copy = inner.copyValue().asValueMap() ?: return inner
        copy.mutableChild("selection").remove("steps")
        val owner = copy.mutableChild("backend").mutableChild(backendKind.token)
        if (owner["steps"] == null) owner["steps"] = combination.token
        return copy
    }

    /**
     * The route branch a canonical owner section declares. Multiple declared
     * branches are resolved in RouteKind catalog order (never HOCON map hash
     * order), so migration is deterministic.
     */
    private fun declaredRoute(backendSection: ValueMap, backendKind: BackendKind): RouteKind? {
        val branches = backendSection[backendKind.token].asValueMap()
            ?.get("route").asValueMap() ?: return null
        val keys = branches.keys.filterIsInstance<String>()
        return RouteKind.entries.firstOrNull { it.token in keys }
    }

    private fun validateSelection(selection: ValueMap): ValueMap {
        requireKeys(selection, LegacySelectionKeys, "selection")
        val out = selection.copyValue().asValueMap() ?: return ValueMap()
        /* S4 R6b: selection.steps is not part of the canonical shape. It is
         * normally migrated into backend.<id>.steps before this runs; a leftover
         * value means the document carried no backend owner and is dropped. */
        out.remove("steps")
        if (out["backend"] == null) out["backend"] = BackendKind.Default.token
        out["backend"]?.let {
            require(it is String && it in BackendTokens) { "selection.backend is not a known backend: $it" }
        }
        if (out["terminal"] == null) out["terminal"] = FrontendKind.RootChild.token
        out["terminal"]?.let {
            require(it is String && it in TerminalTokens) { "selection.terminal is not a known terminal: $it" }
        }
        return out
    }

    private fun validateCommon(common: ValueMap): ValueMap {
        requireKeys(common, CommonKeys, "common")
        /* R6a: an old canonical document may still carry common.fallback_route.
         * It is recognized and ignored, never reported as an unknown key. */
        val out = common.copyValue().asValueMap() ?: ValueMap()
        out.remove("fallback_route")
        return out
    }

    private fun validatePlatform(platform: ValueMap): ValueMap {
        requireKeys(platform, setOf("abi"), "platform")
        platform["abi"]?.let { abi ->
            require(abi is Map<*, *>) { "platform.abi is not an object" }
            val map = abi.asValueMap() ?: ValueMap()
            requireKeys(map, AbiKeys, "platform.abi")
            validateFields(map, "kernel", PlatformKernelKeys, "platform.abi")
            validateFields(map, "task_struct", TaskFields, "platform.abi")
            validateFields(map, "cred", CredFields, "platform.abi")
            validateFields(map, "offset", OffsetFields, "platform.abi")
        }
        return platform.copyValue().asValueMap() ?: ValueMap()
    }

    private fun validateBackend(backend: ValueMap): ValueMap {
        requireKeys(backend, BackendTokens, "backend")
        backend[BackendKind.Cve2026_43499.token]?.let { section ->
            require(section is Map<*, *>) { "backend.cve_2026_43499 is not an object" }
            val map = section.asValueMap() ?: ValueMap()
            requireKeys(map, Backend43499Keys, "backend.cve_2026_43499")
            requireCombinationToken(map, BackendKind.Cve2026_43499)
            validateFields(map, "kernel", BackendKernelKeys, "backend.cve_2026_43499")
            validateFields(map, "cred", CredFields, "backend.cve_2026_43499")
            validateFields(map, "offset", OffsetFields, "backend.cve_2026_43499")
            (map["route"] as? Map<*, *>)?.let { route ->
                for (kind in route.keys) {
                    require(kind.toString() in RouteTokens) {
                        "backend.cve_2026_43499.route.$kind is not a known route"
                    }
                }
            }
        }
        backend[BackendKind.Cve2026_43284.token]?.let { section ->
            require(section is Map<*, *>) { "backend.cve_2026_43284 is not an object" }
            requireCombinationToken(
                section.asValueMap() ?: ValueMap(), BackendKind.Cve2026_43284,
            )
        }
        return backend.copyValue().asValueMap() ?: ValueMap()
    }

    /**
     * S4 R6b: a backend owner's optional `steps` must be a string combination
     * token from that backend's whitelist. An unknown token fails closed with
     * the token text echoed, mirroring the native resolver.
     *
     * This is a HOCON input boundary, so the leniency lives here explicitly:
     * the value is normalized (trim + lower-case) and then resolved exactly
     * ([CombinationCatalog.resolve]); callers that emit the wire write the
     * canonical token, never this spelling.
     */
    private fun requireCombinationToken(section: ValueMap, backendKind: BackendKind) {
        val steps = section["steps"] ?: return
        val normalized = if (steps is String) CombinationCatalog.normalize(steps) else null
        require(normalized != null && CombinationCatalog.resolve(backendKind, normalized) != null) {
            "backend.${backendKind.token}.steps is not a known combination token: $steps"
        }
    }

    /**
     * P1: `plugin.<id>.{enabled,stage,module_path,module_hash,params.*,extract.*}`.
     * Unknown keys, a malformed id or a blank parameter name fail closed with
     * the offending text.
     */
    private fun validatePlugins(section: ValueMap): ValueMap {
        for (id in section.keys) {
            require(PluginPaths.isValidId(id)) { "plugin." + id + ": invalid plugin id" }
            val value = section[id]
            require(value is Map<*, *>) { "plugin." + id + " is not an object" }
            val body = value.asValueMap() ?: ValueMap()
            requireKeys(body, PluginKeys, "plugin." + id)
            for (group in PluginGroupKeys) {
                val entries = body[group] ?: continue
                require(entries is Map<*, *>) { "plugin." + id + "." + group + " is not an object" }
                for (rawName in entries.keys) {
                    val name = rawName as? String
                    require(name != null && name.isNotBlank()) {
                        "plugin." + id + "." + group + ": parameter name must be non-blank text"
                    }
                }
            }
        }
        return section.copyValue().asValueMap() ?: ValueMap()
    }

    private fun validateCountermeasure(section: ValueMap): ValueMap {
        requireKeys(section, CountermeasureKeys, "countermeasure")
        section["vivo_vr_guard"]?.let {
            require(it is Map<*, *>) { "countermeasure.vivo_vr_guard is not an object" }
            requireKeys(it.asValueMap() ?: ValueMap(), VivoGuardKeys, "countermeasure.vivo_vr_guard")
        }
        return section.copyValue().asValueMap() ?: ValueMap()
    }

    private fun validateFields(map: ValueMap, section: String, allowed: Set<String>, path: String) {
        val nested = map[section] as? Map<*, *> ?: return
        requireKeys(nested.asValueMap() ?: ValueMap(), allowed, "$path.$section")
    }

    private fun requireObject(path: String, value: Any?, validate: (ValueMap) -> ValueMap): ValueMap {
        require(value is Map<*, *>) { "$path is not an object" }
        return validate(value.asValueMap() ?: ValueMap())
    }

    private fun requireKeys(map: ValueMap, allowed: Set<String>, path: String) {
        for (key in map.keys) {
            require(key in allowed) { "$path.$key: unknown profile key" }
        }
    }

    // ---- legacy -> canonical ----

    private fun canonicalizeLegacy(raw: ValueMap): ValueMap {
        validateLegacy(raw)
        val out = ValueMap()
        raw["schema_version"]?.let { out["schema_version"] = it }
        raw["release"]?.let { out["release"] = it }
        val full = raw.containsKey("release")

        val common = ValueMap()
        if (raw.containsKey("kernel_major")) common["kernel_major"] = raw["kernel_major"]
        if (raw.containsKey("recommend_vr_guard")) common["vr_guard"] = raw["recommend_vr_guard"]
        if (raw.containsKey("safe_mode")) common["safe_mode"] = raw["safe_mode"]
        /* R6a: fallback.to / fallback_to legacy keys are ignored (known legacy). */
        if (common.isNotEmpty()) out["common"] = common

        if (full) {
            val selection = ValueMap()
            val backend = raw["backend"].asValueMap()
            selection["backend"] = backend?.get("kind") ?: BackendKind.Default.token
            selection["terminal"] = FrontendKind.RootChild.token
            out["selection"] = selection
        }

        val credSplit = splitSection(raw["cred"].asValueMap(), CredFields, PlatformCredKeys)
        val offsetSplit = splitSection(raw["offset"].asValueMap(), OffsetFields, PlatformOffsetKeys)
        val abi = ValueMap()
        raw["task_struct"].asValueMap()?.let { abi["task_struct"] = it.copyValue() }
        credSplit?.first?.let { if (it.isNotEmpty()) abi["cred"] = it }
        offsetSplit?.first?.let { if (it.isNotEmpty()) abi["offset"] = it }
        val platformKernel = ValueMap()
        if (raw.containsKey("kernel_phys_load")) {
            platformKernel["kernel_phys_load"] = raw["kernel_phys_load"]
        }
        if (raw.containsKey("kernel_phys_offset")) {
            platformKernel["kernel_phys_offset"] = raw["kernel_phys_offset"]
        }
        if (platformKernel.isNotEmpty()) abi["kernel"] = platformKernel
        if (abi.isNotEmpty()) out["platform"] = valueMapOf("abi" to abi)

        val be = ValueMap()
        val kernel = ValueMap()
        raw["kernelsnitch"].asValueMap()?.forEach { (key, value) ->
            when (key) {
                "collisions" -> kernel["kernelsnitch_collisions"] = value
                "mm_struct_sz" -> kernel["mm_struct_sz"] = value
            }
        }
        val route = ValueMap()
        fun addRouteField(kind: String, field: String, value: Any?) {
            /* Keep the branch declared even when every field moved to the shared
             * kernel flag, so the primary route stays resolvable. */
            route.mutableChild(kind)
            if (field == "compact_waiter") {
                kernel["compact_waiter"] = value
                if (kind == RouteKind.SELECT_STACK.token) route.mutableChild(kind)[field] = value
            } else {
                route.mutableChild(kind)[field] = value
            }
        }
        raw["route"].asValueMap()?.forEach { (kind, branchRaw) ->
            val branch = branchRaw.asValueMap() ?: ValueMap()
            if (branch.isEmpty()) route.mutableChild(kind)
            branch.forEach { (field, value) -> addRouteField(kind, field, value) }
        }
        /* R6a: fallback.route.<kind>.* legacy geometry is ignored. */
        if (route.isNotEmpty()) be["route"] = route
        if (kernel.isNotEmpty()) be["kernel"] = kernel
        /* S4 R6b: the flat `backend.steps` step id migrates to the owning
         * owner's combination token using the route the profile declares. */
        val flatBackend = raw["backend"].asValueMap()
        val selectedBackend = BackendKind.resolve(
            BackendKind.normalize(flatBackend?.get("kind") as? String),
        ) ?: BackendKind.Default
        val legacySteps = flatBackend?.get("steps") as? String
        /* Deterministic (catalog order), never HOCON map hash order. */
        val declaredRoute = RouteKind.entries.firstOrNull { route.containsKey(it.token) }
        val combination = CombinationCatalog.fromLegacySteps(
            selectedBackend, legacySteps, declaredRoute,
        )
        if (legacySteps != null && combination == null) {
            throw IllegalArgumentException(
                "backend.steps is not a known step set for ${selectedBackend.token}: $legacySteps",
            )
        }
        if (combination != null && selectedBackend == BackendKind.Cve2026_43499) {
            be["steps"] = combination.token
        }
        credSplit?.second?.let { if (it.isNotEmpty()) be["cred"] = it }
        offsetSplit?.second?.let { if (it.isNotEmpty()) be["offset"] = it }
        raw["execution"].asValueMap()?.let { be["execution"] = it.copyValue() }
        val owners = ValueMap()
        if (be.isNotEmpty()) owners[BackendKind.Cve2026_43499.token] = be
        flatBackend?.get(BackendKind.Cve2026_43284.token).asValueMap()?.let { owner43284 ->
            val copy = owner43284.copyValue().asValueMap() ?: owner43284
            if (combination != null && selectedBackend == BackendKind.Cve2026_43284) {
                copy["steps"] = combination.token
            }
            owners[BackendKind.Cve2026_43284.token] = copy
        }
        if (owners.isNotEmpty()) out["backend"] = owners

        raw["vr_guard"].asValueMap()?.let { guard ->
            if (guard.containsKey("tracepoint_funcs")) {
                out["countermeasure"] = valueMapOf(
                    "vivo_vr_guard" to valueMapOf("tracepoint_funcs" to guard["tracepoint_funcs"]),
                )
            }
        }
        return out
    }

    private fun splitSection(
        source: ValueMap?,
        all: Set<String>,
        platformKeys: Set<String>,
    ): Pair<ValueMap, ValueMap>? {
        if (source == null) return null
        val platform = ValueMap()
        val model = ValueMap()
        for ((key, value) in source) {
            if (key !in all) continue
            if (key in platformKeys) platform[key] = value else model[key] = value
        }
        return platform to model
    }

    private fun validateLegacy(raw: ValueMap) {
        for (key in raw.keys) {
            require(key in KnownLegacyTopLevel) { "$key: unknown legacy profile key" }
        }
        raw["task_struct"].asValueMap()?.let { requireKeys(it, TaskFields, "task_struct") }
        raw["cred"].asValueMap()?.let { requireKeys(it, CredFields, "cred") }
        raw["offset"].asValueMap()?.let { requireKeys(it, OffsetFields, "offset") }
        raw["kernelsnitch"].asValueMap()?.let { requireKeys(it, KernelsnitchFields, "kernelsnitch") }
        raw["backend"].asValueMap()?.let {
            requireKeys(it, setOf("steps", "kind", BackendKind.Cve2026_43284.token), "backend")
        }
        raw["route"].asValueMap()?.let { route ->
            for (kind in route.keys) require(kind in RouteTokens) { "route.$kind is not a known route" }
        }
    }

    // ---- canonical -> in-memory logical model ----

    private fun buildRuntime(canonical: ValueMap): ValueMap {
        val out = ValueMap()
        canonical["schema_version"]?.let { out["schema_version"] = it }
        canonical["release"]?.let { out["release"] = it }

        val common = canonical["common"].asValueMap()
        common?.get("kernel_major")?.let { out["kernel_major"] = it }
        common?.get("safe_mode")?.let { out["safe_mode"] = it }
        common?.get("vr_guard")?.let { out["recommend_vr_guard"] = it }

        val selection = canonical["selection"].asValueMap()
        val selectedBackend = BackendKind.resolve(
            BackendKind.normalize(selection?.get("backend") as? String),
        )
            ?: BackendKind.Default
        val backend = ValueMap()
        selection?.get("backend")?.let { backend["kind"] = it }

        val abi = canonical["platform"].asValueMap()?.get("abi").asValueMap()
        abi?.get("task_struct")?.let { out["task_struct"] = it.copyValue() }
        val cred = ValueMap()
        val offset = ValueMap()
        abi?.get("cred").asValueMap()?.forEach { (k, v) -> cred[k] = v }
        abi?.get("offset").asValueMap()?.forEach { (k, v) -> offset[k] = v }
        abi?.get("kernel").asValueMap()?.forEach { (k, v) -> out[k] = v }

        val owners = canonical["backend"].asValueMap()
        val be = owners?.get(BackendKind.Cve2026_43499.token).asValueMap()
        val route = ValueMap()
        be?.get("route").asValueMap()?.forEach { (kind, branch) -> route[kind] = branch.copyValue() }
        val beKernel = be?.get("kernel").asValueMap()
        beKernel?.forEach { (k, v) ->
            when (k) {
                "kernelsnitch_collisions" -> out.mutableChild("kernelsnitch")["collisions"] = v
                "mm_struct_sz" -> out.mutableChild("kernelsnitch")["mm_struct_sz"] = v
            }
        }
        /* The shared kernel compact_waiter flag belongs to the primary route
         * branch in the legacy logical model (native reads it from there). */
        val hasCompactWaiter = beKernel?.containsKey("compact_waiter") == true
        val compactWaiter = beKernel?.get("compact_waiter")
        /* S4 R6b: hoist the single combination token from the selected owner to
         * the flat `backend.steps` selection slot the logical model consumes. */
        val ownerBackend = owners?.get(selectedBackend.token).asValueMap()
        (ownerBackend?.get("steps") as? String)?.let { backend["steps"] = it }
        be?.get("cred").asValueMap()?.forEach { (k, v) -> cred[k] = v }
        be?.get("offset").asValueMap()?.forEach { (k, v) -> offset[k] = v }
        be?.get("execution")?.let { out["execution"] = it.copyValue() }
        if (cred.isNotEmpty()) out["cred"] = cred
        if (offset.isNotEmpty()) out["offset"] = offset
        if (backend.isNotEmpty()) out["backend"] = backend
        owners?.get(BackendKind.Cve2026_43284.token)?.asValueMap()?.let { owner842 ->
            /* The token is hoisted to backend.steps above; keep only the 43284
             * policy/geometry fields in the nested owner view. */
            val copy = owner842.copyValue().asValueMap() ?: owner842
            copy.remove("steps")
            out.mutableChild("backend")[BackendKind.Cve2026_43284.token] = copy
        }

        /* R6a: every declared route branch is the selected geometry; there is
         * no fallback branch to peel off the wire. */
        if (route.isNotEmpty()) {
            if (hasCompactWaiter) {
                route.keys.firstOrNull()?.let { kind ->
                    route.mutableChild(kind)["compact_waiter"] = compactWaiter
                }
            }
            out["route"] = route
        }

        canonical["countermeasure"].asValueMap()
            ?.get("vivo_vr_guard").asValueMap()
            ?.get("tracepoint_funcs")?.let {
                out["vr_guard"] = valueMapOf("tracepoint_funcs" to it)
            }
        return out
    }

    // ---- flattening (equivalence gate) ----

    /** Flattens a canonical map to sorted path -> scalar leaves. */
    fun flatten(canonical: ValueMap): Map<String, Any?> {
        val out = sortedMapOf<String, Any?>()
        fun walk(prefix: String, node: Map<*, *>) {
            for ((rawKey, value) in node) {
                val key = rawKey.toString()
                val path = if (prefix.isEmpty()) key else "$prefix.$key"
                when (value) {
                    is Map<*, *> -> walk(path, value)
                    is List<*> -> value.forEachIndexed { index, item ->
                        if (item is Map<*, *>) walk("$path.$index", item) else out["$path.$index"] = scalar(item)
                    }

                    else -> out[path] = scalar(value)
                }
            }
        }
        walk("", canonical)
        return out
    }

    private fun scalar(value: Any?): Any? = when (value) {
        is Number -> value.toLong()
        else -> value
    }

    private fun fail(path: String): Nothing =
        throw IllegalArgumentException("$path: unknown canonical profile key")
}
