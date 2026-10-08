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
        /* Root scalar of the HOCON refactor (AGENTS: kernel_major / kernel_minor
         * are root keys, kernel_minor optional). The general profiles declare it
         * (design 2.5) and native accepts it; the whitelist simply lacked it,
         * which is what reported "kernel_minor: unknown top-level key". */
        "kernel_minor",
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
        "release", "schema_version", "kernel_major",
    )

    /**
     * Required only while the profile DECLARES the cve_2026_43499 path usable.
     *
     * Design 2.9-1 (user ruling): the must-have parameters are judged per declared
     * path. A general profile that declares only cve_2026_43284 needs no 43499 ABI
     * constants, route or geometry - while a profile that un-comments the 43499
     * declaration is held to every rule below (no blanket relaxation).
     */
    private val RequiredTopLevel43499 = setOf("route", "task_struct", "cred", "offset")
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

    /**
     * True when [profile] declares [backend] usable - THE shared judgement of
     * design 2.9-1, used by fail-closed validation on BOTH sides (the exporter
     * path and the App controller) so the two can never drift apart.
     *
     * `available.<backend>` is the declaration of what this profile may run
     * (design 2.9-1). When the declaration is absent - a pre-declaration / v1
     * document, or the runtime projection that drops `available` - the selected
     * backend (`backend.kind`) decides; anything but an explicit 43284 selection
     * keeps the full 43499 requirements, so no profile is silently relaxed.
     */
    fun declaresBackend(profile: Map<String, Any?>, backend: String): Boolean {
        val available = profile["available"] as? Map<*, *>
        if (available != null && available.isNotEmpty()) {
            return available.containsKey(backend)
        }
        /* No declaration to read: the RUNTIME projection drops `available` (it is
         * a canonical-shape key), and the App path lands here too. Fall back to the
         * backend the document selects - `backend.kind`, the runtime marker - and
         * keep the legacy requirements for everything that is not an explicit 43284
         * selection. */
        val kind = (profile["backend"] as? Map<*, *>)?.get("kind") as? String
        return if (backend == "cve_2026_43499") kind != "cve_2026_43284" else kind == backend
    }

    /** The 43499 declaration; see [declaresBackend]. */
    private fun declares43499(profile: Map<String, Any?>): Boolean =
        declaresBackend(profile, "cve_2026_43499")

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
        /* Design 2.9-1: judge the must-have parameters of the paths the profile
         * DECLARES. Everything 43499-specific below is required only while the
         * declaration lists cve_2026_43499; a section that IS present is still
         * validated field by field, so a half-written ABI block cannot slip
         * through. A profile that un-comments 43499 gets the full rule set back. */
        val needs43499 = declares43499(profile)
        for (key in RequiredTopLevel) {
            if (!profile.containsKey(key)) {
                errors += ConfigError(key, "missing required key")
            }
        }
        if (needs43499) {
            for (key in RequiredTopLevel43499) {
                if (!profile.containsKey(key)) {
                    errors += ConfigError(key, "missing required key")
                }
            }
            if (route == null || route !in RouteNames) {
                errors += ConfigError("route", "missing or unknown route")
            }
        }
        val task = profile["task_struct"] as? Map<*, *>
        if (task == null) {
            if (needs43499) errors += ConfigError("task_struct", "not an object")
        } else if (needs43499) {
            /* The null skeleton a general profile inlines is a carrier for later
             * measurement, not a declaration: while 43499 is not declared usable,
             * its nulls must not fail the must-have check (design 2.9-1). A
             * profile that DECLARES 43499 is held to every field below. */
            for (field in RequiredTaskStruct) {
                if (task[field] !is Number) errors += ConfigError("task_struct.$field", "missing")
            }
        }
        val cred = profile["cred"] as? Map<*, *>
        if (cred == null) {
            if (needs43499) errors += ConfigError("cred", "not an object")
        } else if (needs43499) {
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
            if (needs43499) errors += ConfigError("offset", "not an object")
        } else if (needs43499) {
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
