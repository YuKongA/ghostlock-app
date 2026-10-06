package com.ghostlock.app.data.profile

import com.ghostlock.app.data.getLongAt
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.route.RouteKind

/**
 * Single authority for mapping a merged profile value map onto typed core and
 * execution layers. It owns the route-aware field lookup shared by the app and
 * the Gradle exporter, and the fail-closed structural validation.
 */
object ProfileResolver {
    private val KnownTopLevel = setOf(
        "release", "schema_version", "kernel_major",
        "route", "fallback", "kernelsnitch", "task_struct", "cred", "offset",
        "kernel_phys_load", "kernel_phys_offset", "execution",
        /* Backend-private selection (ADR-0004 R18): backend.steps names the
         * StepSet the cve_2026_43499 backend runs. */
        "backend",
        /* Ancillary vr.ko guard: the gate and the layout section derived from
         * the image's BTF. v1-INPUT TOLERANCE ONLY: v1 documents may still
         * carry these keys, so accepting them here keeps the legacy
         * conversion path working. This does NOT mean the v3 wire carries
         * them -- the vr_guard profile surface was removed in b55708a8 and
         * the vocabulary retired in the native misc.vr_guard batch. */
        "recommend_vr_guard", "vr_guard",
    )
    private val RequiredTopLevel = setOf(
        "release", "schema_version", "kernel_major", "route", "task_struct", "cred", "offset",
    )
    private val RequiredTaskStruct = listOf(
        "prio", "normal_prio", "sched_task_group", "pi_lock", "pi_waiters", "pi_top_task",
        "pi_blocked_on", "pid", "tgid", "atomic_flags", "real_cred", "cred", "comm", "tasks",
        "seccomp",
    )
    private val RequiredCred = listOf("copy_size", "caps_count")
    private val RequiredOffset = listOf(
        "init_task", "init_cred", "root_task_group", "selinux_enforcing",
    )

    /**
     * Canonical native field lookup over the declared route branches. The GLKv3
     * wire carries `recommended_cpus`, so the effective `selected_cpus`
     * is folded into those slots; `compact_waiter`/`pselect_waiter_shift`/
     * `mcast.*` map onto `route.<name>.*`. R6a removed the fallback branch.
     */
    fun nativeValue(
        profile: Map<String, Any?>,
        route: String?,
        path: String,
    ): Long? {
        if (path == "execution.recommended_cpus.main" ||
            path == "execution.recommended_cpus.consumer"
        ) {
            val slot = path.removePrefix("execution.recommended_cpus.")
            profile.getLongAt("execution.selected_cpus.$slot")?.let { return it }
        }
        val branchField = when (path) {
            "compact_waiter" -> "compact_waiter"
            "pselect_waiter_shift" -> "waiter_shift"
            else -> null
        }
        if (branchField != null) {
            route?.let { name ->
                profile.getFlagLongAt("route.$name.$branchField")?.let { return it }
            }
        }
        if (path.startsWith("mcast.")) {
            val field = path.removePrefix("mcast.")
            route?.let { name ->
                profile.getLongAt("route.$name.$field")?.let { return it }
            }
            profile.getLongAt("mcast.$field")?.let { return it }
        }
        /* Also accepts a HOCON boolean for direct dotted paths such as
         * route.tcp_zerocopy.compact_waiter (the controller validates those). */
        return profile.getFlagLongAt(path)
    }

    /**
     * Numeric view that also accepts a HOCON boolean (true=1, false=0); the
     * compact-waiter branch is a boolean in the shipped profiles.
     */
    private fun Map<String, Any?>.getFlagLongAt(path: String): Long? =
        when (val value = getValueAt(path)) {
            is Boolean -> if (value) 1L else 0L
            else -> getLongAt(path)
        }

    /**
     * String accessor over the same dotted-path lookup as [nativeValue]. HOCON
     * tokens such as `backend.steps` cannot ride the numeric accessor; a node
     * that is not a string returns null.
     */
    fun nativeText(profile: Map<String, Any?>, path: String): String? =
        profile.getValueAt(path) as? String

    /**
     * Boolean accessor over the same dotted-path lookup. Accepts a native
     * boolean or the HOCON string spellings true/false/1/0/yes/no/on/off
     * (case-insensitive, surrounding whitespace ignored); anything else is null.
     */
    fun nativeBool(profile: Map<String, Any?>, path: String): Boolean? =
        when (val value = profile.getValueAt(path)) {
            is Boolean -> value
            is String -> when (value.trim().lowercase()) {
                "true", "1", "yes", "on" -> true
                "false", "0", "no", "off" -> false
                else -> null
            }

            else -> null
        }

    /** Fail-closed structural validation of a merged profile. */
    fun validateMerged(
        profile: Map<String, Any?>,
        route: String?,
    ): List<ConfigError> {
        val errors = mutableListOf<ConfigError>()
        for (key in profile.keys) {
            if (key !in KnownTopLevel) {
                errors += ConfigError(key, "unknown top-level key")
            }
        }
        for (key in RequiredTopLevel) {
            if (!profile.containsKey(key)) {
                errors += ConfigError(key, "missing required key")
            }
        }
        if (route == null || route !in RouteNames) {
            errors += ConfigError("route", "missing or unknown route")
        }
        val task = profile["task_struct"] as? Map<*, *>
        if (task == null) {
            errors += ConfigError("task_struct", "not an object")
        } else {
            for (field in RequiredTaskStruct) {
                if (task[field] !is Number) errors += ConfigError("task_struct.$field", "missing")
            }
        }
        val cred = profile["cred"] as? Map<*, *>
        if (cred == null) {
            errors += ConfigError("cred", "not an object")
        } else {
            for (field in RequiredCred) {
                when (val value = cred[field]) {
                    null -> errors += ConfigError("cred.$field", "missing")
                    !is Number -> errors += ConfigError("cred.$field", "not a number")
                    else -> if (value.toLong() == 0L) {
                        errors += ConfigError("cred.$field", "zero")
                    }
                }
            }
        }
        val offset = profile["offset"] as? Map<*, *>
        if (offset == null) {
            errors += ConfigError("offset", "not an object")
        } else {
            for (field in RequiredOffset) {
                when (val value = offset[field]) {
                    null -> errors += ConfigError("offset.$field", "missing")
                    !is Number -> errors += ConfigError("offset.$field", "not a number")
                    else -> if (value.toLong() == 0L) {
                        errors += ConfigError("offset.$field", "zero")
                    }
                }
            }
        }
        /* Narrow wire fields: reject values the native widths cannot carry
         * instead of letting the typed casts wrap them (the poison/walk tuning
         * is u8/u8/u16). This is the shared validation, so the exporter and
         * every other caller are covered too.
         *
         * The vr_guard layout bound is GONE on purpose: b55708a8 removed the
         * vr_guard profile surface, so the v3 document no longer carries
         * "vr_guard.tracepoint_funcs" and a width check for it would be a dead
         * mapping (it could never fire). Re-add only together with the field. */
        val widths = buildList {
            if (RouteKind.resolve(route) == RouteKind.MULTICAST_WAITER) {
                add("route.multicast_waiter.attempts" to 0xffL)
                add("route.multicast_waiter.arm_sequence" to 0xffL)
                add("route.multicast_waiter.arm_hold" to 0xffffL)
            }
        }
        for ((path, max) in widths) {
            val value = profile.getLongAt(path) ?: continue
            if (value < 0L || value > max) {
                errors += ConfigError(path, "outside 0..$max")
            }
        }
        return errors
    }

    /** Flattens the `execution` object into path -> value. */
    fun executionFromMerged(profile: Map<String, Any?>): Map<String, ULong> {
        val execution = profile["execution"] as? Map<*, *> ?: return emptyMap()
        val out = LinkedHashMap<String, ULong>()
        fun walk(prefix: String, node: Map<*, *>) {
            for ((rawKey, value) in node) {
                val key = rawKey.toString()
                val path = if (prefix.isEmpty()) key else "$prefix.$key"
                when (value) {
                    is Map<*, *> -> walk(path, value)
                    is Number -> out[path] = value.toLong().toULong()
                }
            }
        }
        walk("", execution)
        return out
    }

    private val RouteNames = RouteKind.entries.map { it.token }
}
