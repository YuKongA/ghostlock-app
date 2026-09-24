package com.ghostlock.app.data.profile

import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.getLongAt
import com.ghostlock.app.data.route.RouteKind

/**
 * Single authority for mapping a merged profile value map onto typed core and
 * execution layers. It owns the route-aware field lookup shared by the app and
 * the Gradle exporter, and the fail-closed structural validation.
 */
object ProfileResolver {
    private val KnownTopLevel = setOf(
        "release", "schema_version", "kernel_major", "recommend_shizuku",
        "route", "fallback", "kernelsnitch", "task_struct", "cred", "offset",
        "kernel_phys_load", "execution",
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
     * Canonical native field lookup over the declared route branches. The v2
     * transport carries `recommended_cpus`, so the effective `selected_cpus`
     * is folded into those slots; `compact_waiter`/`pselect_waiter_shift`/
     * `mcast.*` map onto `route.<name>.*` then `fallback.route.<name>.*`.
     */
    fun nativeValue(
        profile: Map<String, Any?>,
        route: String?,
        fallbackTo: String?,
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

    /** Fail-closed structural validation of a merged profile. */
    fun validateMerged(
        profile: Map<String, Any?>,
        route: String?,
        fallbackTo: String?,
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
        if (fallbackTo != null && fallbackTo != "none" && fallbackTo !in RouteNames) {
            errors += ConfigError("fallback.to", "unknown fallback route")
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
                if ((cred[field] as? Number)?.toLong() == 0L) {
                    errors += ConfigError("cred.$field", "missing or zero")
                }
            }
        }
        val offset = profile["offset"] as? Map<*, *>
        if (offset == null) {
            errors += ConfigError("offset", "not an object")
        } else {
            for (field in RequiredOffset) {
                if ((offset[field] as? Number)?.toLong() == 0L) {
                    errors += ConfigError("offset.$field", "missing or zero")
                }
            }
        }
        return errors
    }

    /** Extracts the typed core from a (validated) merged profile. */
    fun coreFromMerged(
        release: String,
        profile: Map<String, Any?>,
        route: RouteKind?,
        fallbackTo: RouteKind?,
    ): CoreProfile {
        val document = NativeProfileDocument.from(
            release = release,
            route = route?.token,
            fallbackTo = fallbackTo?.token,
        ) { path -> nativeValue(profile, route?.token, fallbackTo?.token, path) }
        return CoreProfile(
            release = release,
            schemaVersion = (profile["schema_version"] as? Number)?.toInt() ?: 1,
            kernelMajor = document.kernelMajor,
            taskStruct = document.taskStruct,
            cred = document.cred,
            offsets = document.kernelOffset,
            kernelPhysLoad = document.kernelPhysLoad,
            route = RouteKind.fromWire(document.routeKind),
            fallback = RouteKind.fromWire(document.fallbackRoute),
            kernelsnitchCollisions = document.kernelsnitchCollisions,
            mmStructSz = document.mmStructSz,
            recommendations = SparseExecutionValues(executionFromMerged(profile)),
        )
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
                    is Number -> out[path] = value.toLong().coerceAtLeast(0L).toULong()
                }
            }
        }
        walk("", execution)
        return out
    }

    private val RouteNames = RouteKind.entries.map { it.token }
}
