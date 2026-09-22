package com.ghostlock.app.data

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
 * `fallback_to`). 5.x branches are only kept for those local formats; nothing
 * in a remote/main document can select them.
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
        "init_task", "init_cred", "empty_zero_page", "mcast_fake_bss",
        "root_task_group", "selinux_enforcing", "selinux_blob_sizes",
        "security_hook_heads", "slide_nfulnl_logger", "slide_loggers_0_1",
        "slide_boot_id",
    )
    private val McastFields = setOf(
        "waiter_off", "buffer_size", "task_offset", "lock_offset",
        "fake_lock_offset", "fake_task_offset", "lock_slots_offset",
        "lock_slot_count", "lock_slot_stride",
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

    private object MulticastRouteCodec : LegacyRouteCodec {
        override fun fillBranch(entry: ValueMap, branch: ValueMap) {
            moveKey(entry, branch, "compact_waiter", "compact_waiter")
            moveMcast(entry, branch)
        }

        override fun fillFallback(entry: ValueMap, branch: ValueMap) = moveMcast(entry, branch)

        private fun moveMcast(entry: ValueMap, branch: ValueMap) {
            entry["mcast"].asValueMap()?.let { mcast ->
                mcast.forEach { (key, value) ->
                    if (!branch.containsKey(key)) branch[key] = value
                }
                entry.remove("mcast")
            }
        }
    }

    private val RouteCodecs: Map<String, LegacyRouteCodec> = mapOf(
        "tcp_zerocopy" to TcpRouteCodec,
        "select_stack" to SelectRouteCodec,
        "multicast_waiter" to MulticastRouteCodec,
    )

    fun convertValue(entry: ValueMap?): ValueMap? {
        if (entry == null) return null
        MetadataKeys.forEach(entry::remove)
        moveFlatNamespaces(entry)
        moveSymbolGroups(entry)
        moveKernelsnitch(entry)
        moveRouteLayout(entry)
        moveFallback(entry)
        dropEmptyRouteBranches(entry)
        return entry
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
        moveGroup(entry, "struct_fields", "mcast", "mcast_", McastFields)
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
            Triple("mcast", "mcast_", McastFields),
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
            entry.remove("mcast")
            return
        }
        if (entry["fallback"].asValueMap() != null || entry["route"].asValueMap() == null) {
            entry.remove("compact_waiter")
            entry.remove("pselect_waiter_shift")
            entry.remove("mcast")
            return
        }
        if (!entry.containsKey("release")) {
            /* Sparse override fragments must not gain an inferred fallback:
             * it would overwrite the choice kept in the offsets entry. */
            entry.remove("compact_waiter")
            entry.remove("pselect_waiter_shift")
            entry.remove("mcast")
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
        entry.remove("mcast")
    }

    private fun inferRoute(entry: ValueMap): String {
        /* Remote/main-era documents are 6.x only, so 5.x inference is a
         * guarded fallback for local transition files rather than a path a
         * report can take. */
        val major = (entry["kernel_major"] as? Number)?.toLong() ?: 0L
        val waiter = (entry["mcast_waiter_off"] as? Number)?.toLong() ?: 0L
        if (major == 5L && waiter > 0L) return "multicast_waiter"
        if (((entry["compact_waiter"] as? Number)?.toLong() ?: 0L) != 0L) return "tcp_zerocopy"
        return "select_stack"
    }
}
