package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.plugin.PluginPaths
import com.ghostlock.app.data.route.RouteKind

/**
 * S4 R3 canonical HOCON layout and parse-time normalization.
 *
 * Bundled profiles live in the canonical layout (Wrapper root: ghostlock
 * { schema_version release kernel_major [kernel_minor] [safe_mode] available
 * backend.<id> }). The parser accepts both that layout and the legacy flat one;
 * every document is normalized at parse time into a single canonical form and
 * then projected onto the in-memory logical model the resolver/UI already
 * consume (toRuntime). Unrecognized keys fail closed with their dotted path
 * instead of being dropped.
 *
 * HOCON refactor (2026-10-05, native b55708a8): the `common`, `platform` and
 * `countermeasure` owners and the `selection` block are GONE — a document that
 * still carries any of them is rejected by [canonicalizeCanonical]. The kernel
 * scalars moved to the profile root (kernel_minor is optional and unused today),
 * `platform.abi.*` moved under the owning backend
 * (`backend.cve_2026_43499.abi.*`), the old `selection` became `available`
 * (the profile DECLARES what may run; the user/App picks at run time and writes
 * the token into `backend.<id>.steps`), and `vr_guard` disappeared entirely.
 *
 * Non-identity alias exceptions (legacy flat -> canonical):
 *  - backend.steps (legacy step id) -> the owning backend.<id>.steps token;
 *  - kernel_major / kernel_minor / safe_mode -> the profile root;
 *  - fallback.to / fallback.route.<kind>.* are recognized-but-ignored legacy
 *    keys (R6a removed route fallback from the wire); they never fail as
 *    unrecognized and never reach the runtime model;
 *  - kernelsnitch.collisions -> backend.cve_2026_43499.kernel.kernelsnitch_collisions;
 *  - kernelsnitch.mm_struct_sz -> backend.cve_2026_43499.kernel.mm_struct_sz;
 *  - cred is split: ABI cred -> backend.cve_2026_43499.abi.cred, the 43499
 *    template -> backend.cve_2026_43499.cred;
 *  - offset is split: ABI symbols -> backend.cve_2026_43499.abi.offset,
 *    slide anchors -> backend.cve_2026_43499.offset;
 *  - kernel_phys_load|offset -> backend.cve_2026_43499.abi.kernel.*;
 *  - task_struct -> backend.cve_2026_43499.abi.task_struct;
 *  - route.<kind>.<field> -> backend.cve_2026_43499.route.<kind>.<field>;
 *  - route.<tcp_zerocopy|multicast_waiter>.compact_waiter -> the shared
 *    backend.cve_2026_43499.kernel.compact_waiter wire flag;
 *    route.select_stack.compact_waiter keeps its route field and sets the flag;
 *  - execution.* -> backend.cve_2026_43499.execution.*;
 *  - recommend_vr_guard / vr_guard.* are legacy-only now: recognized and
 *    IGNORED (the profile surface was deleted with the feature).
 */
object ProfileLayout {
    /** Canonical root object wrapping a device profile. */
    const val Wrapper: String = "ghostlock"

    private val BackendTokens: Set<String> = linkedSetOf(
        BackendKind.Cve2026_43499.token,
        BackendKind.Cve2026_43284.token,
    )

    private val RouteTokens: Set<String> = RouteKind.entries.mapTo(linkedSetOf()) { it.token }

    /**
     * M2 availability selection (design doc 4.5 / 5-Q1-Q3): the OBJECT form of
     * `available.<backend>` declares the queue-level route token, the step queue
     * and the static experimental opt-in. [carryAvailableSelection] is the ONE
     * place that copies them into the backend owner the wire reads them from;
     * the legacy token LIST form declares none of them and copies nothing.
     */
    private val AvailableSelectionKeys = listOf("route", "queue", "experimental")

    /** Queue element keys; `params` is reserved-but-unimplemented (U10). */
    private val QueueElementKeys = listOf("step", "seam", "stage")

    /**
     * Canonical slot for the queue-level route STRING when the backend owner
     * already carries the per-route geometry MAP under `route`
     * (`route.<kind>.<field>`, every bundled profile does). The wire keeps the
     * two apart — key `route` versus the `route.<branch>.*` sections — so the
     * canonical map needs its own slot; `NativeProfile` maps this back onto the
     * wire key `route` (the ONE mapping point). With no geometry map the string
     * uses `route` directly. Name follows the native declaration's own wording
     * ("Queue-level route token", `backend/cve_2026_43499/schema.hpp`). */
    private const val QueueRouteKey = "queue_route"

    /**
     * Canonical ROOT keys (HOCON refactor): the wrapper value may only carry
     * these scalars plus the structural blocks. kernel_minor is optional and
     * unused today; nothing derives from it yet (the user's ruling).
     */
    private val RootKeys = setOf(
        "schema_version", "release", "kernel_major", "kernel_minor", "safe_mode",
    )
    private val AbiKeys = setOf("kernel", "task_struct", "cred", "offset")
    private val AbiKernelKeys = setOf("kernel_phys_load", "kernel_phys_offset")
    private val BackendKernelKeys = setOf(
        "compact_waiter", "kernelsnitch_collisions", "mm_struct_sz",
    )
    private val Backend43499Keys =
        setOf("steps", "kernel", "cred", "offset", "route", "execution", "abi")
    /**
     * The 43284 owner declares ONLY the selection token and the execution
     * tuning. kmi / lkm_path / carrier_path are native-side conventions (the
     * wire keeps them, the profile must not provide them) and the old flat
     * execution keys moved under `execution`, so anything else fails closed.
     */
    private val Backend43284Keys = setOf("steps", "execution")
    private val Backend43284ExecutionKeys = setOf(
        "late_load_args", "selinux_exec_context", "module_poll_attempts",
        "module_poll_interval_ms", "wait_timeout_ms",
    )

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

    /** The ABI half of a legacy flat `cred`/`offset` section. */
    private val AbiCredKeys = setOf(
        "usage_offset", "caps_offset", "ref_count",
        "ref0_offset", "ref1_offset", "ref2_offset", "ref3_offset",
    )
    private val AbiOffsetKeys = setOf(
        "init_task", "init_cred", "empty_zero_page", "root_task_group",
        "selinux_enforcing", "selinux_blob_sizes", "security_hook_heads",
    )

    private val KnownLegacyTopLevel = setOf(
        "schema_version", "release", "kernel_major", "backend",
        "kernel_phys_load", "kernel_phys_offset", "route", "fallback", "fallback_to",
        "kernelsnitch", "task_struct", "cred", "offset", "execution",
        "kernel_minor", "safe_mode",
        "symbols", "struct_fields", "kimage_text_base", "btf_size", "kallsyms",
    )

    /** True when [raw] is already in the canonical owner-qualified layout. */
    fun isCanonical(raw: Map<*, *>): Boolean {
        if (raw.containsKey(Wrapper)) return true
        if (raw.keys.any { it.toString() == "available" }) {
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
        val inner = raw[Wrapper].asValueMap() ?: raw
        val out = ValueMap()
        /* The availability declaration is validated first, whatever its position
         * in the map: the backend owner check must know which selection keys
         * this document legitimately declares (see [carryAvailableSelection]). */
        val available = inner["available"]?.let {
            requireObject("available", it) { value -> validateAvailable(value) }
        }
        for (key in inner.keys) {
            val value = inner[key]
            when (key) {
                "schema_version", "release" -> out[key] = value
                "kernel_major", "kernel_minor" -> {
                    require(value is Number) { "$key must be a number" }
                    out[key] = value
                }

                "safe_mode" -> {
                    require(value is Boolean) { "safe_mode must be a boolean" }
                    out[key] = value
                }

                "available" -> out[key] = requireNotNull(available) { "available is not an object" }
                "backend" -> out[key] = requireObject("backend", value) { validateBackend(it, available) }
                "plugin" -> out[key] = requireObject("plugin", value) { validatePlugins(it) }
                /* HOCON refactor: common / platform / selection / countermeasure
                 * are gone — their names land here and fail closed. */
                else -> fail(key)
            }
        }
        carryAvailableSelection(out)
        return out
    }

    /**
     * M2 queue carrying (design doc 4.5 / 5-Q2): the ONE place where the
     * `available.<backend>` object form's selection keys are copied into the
     * backend owner the wire reads them from (`backend.<id>.{route,queue,
     * experimental}`). The token LIST form copies nothing, so every existing
     * document keeps its exact bytes; an undeclared key stays absent (presence
     * is key occurrence).
     *
     * `backend.<id>.route` is ALREADY the per-route geometry map
     * (`route.<kind>.<field>`, validated below) while the queue-level route is a
     * plain STRING: when the geometry map owns the key the string rides
     * [QueueRouteKey] instead (see [carryQueueRoute]), so the geometry is never
     * overwritten and the route is never dropped. */
    private fun carryAvailableSelection(out: ValueMap) {
        val available = out["available"].asValueMap() ?: return
        val owners = out["backend"].asValueMap() ?: ValueMap().also { out["backend"] = it }
        for ((backend, declaration) in available) {
            val selection = declaration.asValueMap() ?: continue
            val owner = owners.mutableChild(backend)
            selection["queue"]?.let { owner["queue"] = it.copyValue() }
            selection["experimental"]?.let { owner["experimental"] = it.copyValue() }
            selection["route"]?.let { carryQueueRoute(owner, backend, it as String) }
        }
    }

    /**
     * Carries the queue-level route STRING into the backend owner without ever
     * touching the per-route geometry MAP that already owns `route`:
     *
     * - no `route` key yet (43284, or a 43499 profile without geometry) => write
     *   `route` directly;
     * - `route` already a STRING (a re-canonicalized map) => it must equal the
     *   declaration, and [QueueRouteKey] must be absent (two strings would need
     *   an arbitrary precedence => fail closed);
     * - `route` a geometry map => the string goes to [QueueRouteKey] instead,
     *   equal-or-absent on the second pass.
     */
    private fun carryQueueRoute(owner: ValueMap, backend: String, route: String) {
        when (val existing = owner["route"]) {
            null -> owner["route"] = route
            is String -> {
                require(owner[QueueRouteKey] == null) {
                    "backend.$backend: both route and $QueueRouteKey are strings; " +
                        "refusing to pick a precedence"
                }
                require(existing == route) {
                    "backend.$backend.route conflicts with available.$backend.route: " +
                        "$existing != $route"
                }
            }

            else -> {
                val carried = owner[QueueRouteKey]
                require(carried == null || carried == route) {
                    "backend.$backend.$QueueRouteKey conflicts with available.$backend.route: " +
                        "$carried != $route"
                }
                owner[QueueRouteKey] = route
            }
        }
    }

    /** The declared selection keys of one backend, or an empty map (token list). */
    private fun selectionEchoes(available: ValueMap?, backend: String): Map<String, Any?> =
        available?.get(backend).asValueMap() ?: emptyMap()

    /**
     * The canonical keys a declared selection may occupy: the key itself, plus
     * [QueueRouteKey] for the queue-level route (its String slot when the
     * geometry map owns `route`).
     */
    private fun selectionEchoKeys(declaration: Map<String, Any?>): Set<String> = buildSet {
        for (key in AvailableSelectionKeys) {
            if (!declaration.containsKey(key)) continue
            add(key)
            if (key == "route") add(QueueRouteKey)
        }
    }

    /**
     * An echo is legal only while it is deep-equal to the declaration: keys the
     * document did not declare are unknown keys (checked by the caller), and a
     * disagreeing echo fails closed with its path instead of winning silently.
     */
    private fun requireSelectionEchoes(
        map: ValueMap,
        declaration: Map<String, Any?>,
        path: String,
    ) {
        for (key in selectionEchoKeys(declaration)) {
            val declared = declaration[if (key == QueueRouteKey) "route" else key] ?: continue
            /* `route` may hold the geometry map; only a String is an echo. */
            val echo = when (key) {
                "route" -> map[key].takeIf { it is String }
                else -> map[key]
            } ?: continue
            require(echo == declared) {
                "$path.$key conflicts with the available declaration: $echo != $declared"
            }
        }
    }

    /**
     * `available`: what the profile ALLOWS. Two shapes, both kept:
     *
     * - the legacy LIST of combination tokens (the 68 bundled assets): the
     *   profile only declares, the user/App picks at run time and the choice is
     *   written into `backend.<id>.steps`; each token is resolved in the shared
     *   catalogue, so a typo fails closed here instead of on the device;
     * - the M2 OBJECT form (design doc 4.5 / 5-Q1-Q3 / 5-Q2) declaring the
     *   queue-level route (`route`), the step queue (`queue`) and the static
     *   experimental opt-in (`experimental`) — see
     *   [validateAvailableSelection]. [carryAvailableSelection] copies exactly
     *   those three into the backend owner the wire reads them from.
     */
    private fun validateAvailable(available: ValueMap): ValueMap {
        val out = ValueMap()
        for ((token, rawTokens) in available) {
            require(token in BackendTokens) { "available.$token: unknown backend" }
            val kind = requireNotNull(BackendKind.resolve(token))
            if (rawTokens is Map<*, *>) {
                out[token] = validateAvailableSelection(token, rawTokens.asValueMap() ?: ValueMap())
                continue
            }
            val list = rawTokens as? List<*>
            require(list != null && list.isNotEmpty()) {
                "available.$token must be a non-empty list of combination tokens"
            }
            out[token] = list.map { rawToken ->
                val text = rawToken as? String
                val normalized = text?.let(CombinationCatalog::normalize)
                require(normalized != null && CombinationCatalog.resolve(kind, normalized) != null) {
                    "available.$token: not a known combination token: $rawToken"
                }
                normalized
            }
        }
        return out
    }

    /**
     * The M2 object form of one backend's declaration. Exactly the three
     * selection keys are allowed; `route` is a known [RouteKind] (stored
     * normalized, like the token list), `experimental` is a Boolean and
     * `queue` is a non-empty list of step objects.
     */
    private fun validateAvailableSelection(backend: String, selection: ValueMap): ValueMap {
        val out = ValueMap()
        for ((key, raw) in selection) {
            require(key in AvailableSelectionKeys) { "available.$backend.$key: unknown key" }
            when (key) {
                "route" -> {
                    val route = (raw as? String)?.let { RouteKind.resolve(RouteKind.normalize(it)) }
                    require(route != null) { "available.$backend.route: not a known route: $raw" }
                    out[key] = route.token
                }

                "experimental" -> {
                    require(raw is Boolean) { "available.$backend.experimental: must be a boolean" }
                    out[key] = raw
                }

                "queue" -> out[key] = validateQueue(backend, raw)
            }
        }
        return out
    }

    /**
     * One queue element is `{ step = "<id>" }` or `{ seam = "<type>",
     * stage = "<stage>" }` (design 5-Q1/5-Q3): a plain string, a non-map, an
     * unknown key, both or neither selector, `stage` without `seam`, a
     * non-string value and the reserved `params` all fail closed with their
     * dotted path. An empty queue is rejected too: "do nothing" is not an
     * attack declaration (design 4.2c). */
    private fun validateQueue(backend: String, raw: Any?): List<Any?> {
        val list = raw as? List<*>
        require(list != null && list.isNotEmpty()) {
            "available.$backend.queue must be a non-empty list of step objects"
        }
        return list.mapIndexed { index, element ->
            val path = "available.$backend.queue[$index]"
            require(element is Map<*, *>) { "$path: queue-element-not-object" }
            val map = element.asValueMap() ?: ValueMap()
            for (key in map.keys) {
                require(key in QueueElementKeys || key == "params") { "$path.$key: unknown key" }
            }
            require(map["params"] == null) {
                "$path.params: params-reserved-for-future-step-parameters"
            }
            val step = map["step"]
            val seam = map["seam"]
            require((step == null) != (seam == null)) {
                "$path: queue-element-needs-exactly-one-of-step-seam"
            }
            if (map["stage"] != null) {
                require(seam != null) { "$path.stage: stage-requires-seam" }
            }
            for (key in QueueElementKeys) {
                val value = map[key] ?: continue
                require(value is String) { "$path.$key: value must be a string" }
            }
            /* Deterministic key order (step, seam, stage); every value is a
             * validated string, so nothing is dropped here. */
            val out = ValueMap()
            for (key in QueueElementKeys) {
                (map[key] as? String)?.let { out[key] = it }
            }
            out
        }
    }

    /**
     * @param available the validated `available` declaration, when the document
     * has one: the canonical backend owner may ECHO the selection keys it
     * carries ([carryAvailableSelection] writes them there), and only an echo
     * that is deep-equal to the declaration is legal — so
     * `canonicalize(canonicalize(m)) == canonicalize(m)` holds while a
     * hand-written `backend.<id>.queue` without a declaration is still an
     * unknown key.
     */
    private fun validateBackend(backend: ValueMap, available: ValueMap?): ValueMap {
        requireKeys(backend, BackendTokens, "backend")
        backend[BackendKind.Cve2026_43499.token]?.let { section ->
            require(section is Map<*, *>) { "backend.cve_2026_43499 is not an object" }
            val map = section.asValueMap() ?: ValueMap()
            val echoes = selectionEchoes(available, BackendKind.Cve2026_43499.token)
            requireKeys(map, Backend43499Keys + selectionEchoKeys(echoes), "backend.cve_2026_43499")
            requireSelectionEchoes(map, echoes, "backend.cve_2026_43499")
            requireCombinationToken(map, BackendKind.Cve2026_43499)
            validateFields(map, "kernel", BackendKernelKeys, "backend.cve_2026_43499")
            validateFields(map, "cred", CredFields, "backend.cve_2026_43499")
            validateFields(map, "offset", OffsetFields, "backend.cve_2026_43499")
            /* HOCON refactor: platform.abi.* lives here now. */
            (map["abi"] as? Map<*, *>)?.let { abiRaw ->
                val abi = abiRaw.asValueMap() ?: ValueMap()
                requireKeys(abi, AbiKeys, "backend.cve_2026_43499.abi")
                validateFields(abi, "kernel", AbiKernelKeys, "backend.cve_2026_43499.abi")
                validateFields(abi, "task_struct", TaskFields, "backend.cve_2026_43499.abi")
                validateFields(abi, "cred", CredFields, "backend.cve_2026_43499.abi")
                validateFields(abi, "offset", OffsetFields, "backend.cve_2026_43499.abi")
            }
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
            val map = section.asValueMap() ?: ValueMap()
            /* Fail closed on kmi / lkm_path / carrier_path (native-side
             * conventions) and on any other unknown key; the declared selection
             * keys may only ride as equal echoes (see [validateBackend]). */
            val echoes = selectionEchoes(available, BackendKind.Cve2026_43284.token)
            requireKeys(map, Backend43284Keys + selectionEchoKeys(echoes), "backend.cve_2026_43284")
            requireSelectionEchoes(map, echoes, "backend.cve_2026_43284")
            requireCombinationToken(map, BackendKind.Cve2026_43284)
            validateFields(map, "execution", Backend43284ExecutionKeys, "backend.cve_2026_43284")
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

        /* HOCON refactor: the kernel scalars live at the profile root; the old
         * common.vr_guard / recommend_vr_guard keys are legacy-only and ignored
         * (the profile surface was deleted with the feature). fallback.to /
         * fallback_to stay recognized-and-ignored (R6a). */
        raw["kernel_major"]?.let { out["kernel_major"] = it }
        raw["kernel_minor"]?.let { out["kernel_minor"] = it }
        raw["safe_mode"]?.let { out["safe_mode"] = it }

        val credSplit = splitSection(raw["cred"].asValueMap(), CredFields, AbiCredKeys)
        val offsetSplit = splitSection(raw["offset"].asValueMap(), OffsetFields, AbiOffsetKeys)
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

        val be = ValueMap()
        /* HOCON refactor: platform.abi.* is owned by the 43499 backend now. */
        if (abi.isNotEmpty()) be["abi"] = abi
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
        /* The legacy document pins one token; the canonical shape DECLARES it as
         * the allowed set (or the catalogue default when nothing was pinned). */
        if (full) {
            val allowed = combination?.token
                ?: CombinationCatalog.defaultFor(selectedBackend)?.token
            if (allowed != null) {
                out["available"] = valueMapOf(selectedBackend.token to listOf(allowed))
            }
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
            /* HOCON refactor: the flat execution keys move under `execution`, and
             * kmi / lkm_path / carrier_path are native-side conventions the
             * profile must not carry. */
            val execution = copy["execution"].asValueMap()?.copyValue()?.asValueMap() ?: ValueMap()
            for (key in Backend43284ExecutionKeys) {
                copy[key]?.let { execution[key] = it }
                copy.remove(key)
            }
            if (execution.isNotEmpty()) copy["execution"] = execution
            for (key in listOf("kmi", "lkm_path", "carrier_path")) copy.remove(key)
            owners[BackendKind.Cve2026_43284.token] = copy
        }
        if (owners.isNotEmpty()) out["backend"] = owners

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

    /* S4 hotfix: this validates LEGACY input, i.e. maps written by OLDER revisions
     * (persisted HOCON / prefs) that we cannot retroactively fix. An unknown key is
     * therefore dropped with a diagnostic instead of aborting: a fail-closed require()
     * here turns an app upgrade into a launch crash (observed on a real device:
     * `recommend_shizuku: unknown legacy profile key`). The precedent is already in this
     * file: `fallback_to` / `fallback.to` are known-legacy and deliberately ignored.
     * Fail-closed stays where this revision authors the input (canonical validation). */
    private fun validateLegacy(raw: ValueMap) {
        for (key in raw.keys.filter { it !in KnownLegacyTopLevel }) {
            System.err.println("ghostlock: ignoring unknown legacy profile key '$key'")
        }
        dropUnknownLegacyKeys(raw["task_struct"].asValueMap(), TaskFields, "task_struct")
        dropUnknownLegacyKeys(raw["cred"].asValueMap(), CredFields, "cred")
        dropUnknownLegacyKeys(raw["offset"].asValueMap(), OffsetFields, "offset")
        dropUnknownLegacyKeys(raw["kernelsnitch"].asValueMap(), KernelsnitchFields, "kernelsnitch")
        dropUnknownLegacyKeys(
            raw["backend"].asValueMap(),
            setOf("steps", "kind", BackendKind.Cve2026_43284.token),
            "backend",
        )
        raw["route"].asValueMap()?.let { route ->
            for (kind in route.keys.filter { it !in RouteTokens }) {
                System.err.println("ghostlock: ignoring unknown legacy route '$kind'")
                route.remove(kind)
            }
        }
    }

    /** Drops keys a newer revision no longer knows; legacy input must never abort. */
    private fun dropUnknownLegacyKeys(map: ValueMap?, allowed: Set<String>, path: String) {
        if (map == null) return
        for (key in map.keys.filter { it !in allowed }) {
            System.err.println("ghostlock: ignoring unknown legacy key '$path.$key'")
            map.remove(key)
        }
    }

    // ---- canonical -> in-memory logical model ----

    private fun buildRuntime(canonical: ValueMap): ValueMap {
        val out = ValueMap()
        canonical["schema_version"]?.let { out["schema_version"] = it }
        canonical["release"]?.let { out["release"] = it }

        /* Root scalars (HOCON refactor). kernel_minor is carried through for the
         * UI/future use; nothing derives from it yet. */
        canonical["kernel_major"]?.let { out["kernel_major"] = it }
        canonical["kernel_minor"]?.let { out["kernel_minor"] = it }
        canonical["safe_mode"]?.let { out["safe_mode"] = it }

        /* The DECLARED availability fixes the default backend; the App lets the
         * user pick among the declared tokens at run time. */
        val available = canonical["available"].asValueMap() ?: ValueMap()
        val selectedBackend = BackendKind.entries.firstOrNull { it.token in available.keys }
            ?: BackendKind.Default
        val backend = ValueMap()
        if (selectedBackend.token in available.keys) backend["kind"] = selectedBackend.token

        val abi = canonical["backend"].asValueMap()
            ?.get(BackendKind.Cve2026_43499.token).asValueMap()
            ?.get("abi").asValueMap()
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
