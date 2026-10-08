package com.ghostlock.app.data

/**
 * Design 2.8.A (user ruling A, 2026-10-06): the explicit `priority` key in
 * `available.<backend>` REPLACES the implicit "declaration order is priority"
 * rule of design 2.8 - the backend submenu is ordered by this number instead of
 * by map iteration order, which `R5-ORDER-01` measured to be unstable through the
 * parse stage.
 *
 * Rules implemented here (the single authority for selection order):
 *  - `priority` is a POSITIVE integer; SMALLEST first (highest priority);
 *  - an entry WITHOUT `priority` sorts LAST (lowest priority, design 2.8.A);
 *  - entries without a priority keep a deterministic relative order (by backend
 *    token), so a profile that declares none is still stable;
 *  - the submenu default (design 2.8: "unselected => the declared default") is the
 *    FIRST entry of that order;
 *  - two entries with the SAME explicit priority are a CONFIGURATION ERROR
 *    (fail closed): design 2.8 does not define a tie-break, and picking one
 *    arbitrarily would reintroduce exactly the order dependence that 2.8.A
 *    removes.
 *
 * Scope: selection/presentation only. `priority` is never carried into the
 * canonical backend owner and never reaches the GLKv3 wire (design 2.8: "this
 * clause constrains the Kotlin selection only").
 */
object AvailablePriority {
    /** The declaration key; part of the `available.<backend>` object form. */
    const val Key = "priority"

    /**
     * Backend tokens of [available] in priority order: smallest explicit
     * `priority` first, then the entries that declare none (by token, so the
     * result does not depend on map iteration order).
     *
     * @throws IllegalArgumentException when two entries declare the same priority.
     */
    fun orderedBackends(available: Map<*, *>?): List<String> {
        val declarations = available?.entries.orEmpty().mapNotNull { (rawBackend, raw) ->
            val backend = rawBackend as? String ?: return@mapNotNull null
            backend to priorityOf(backend, raw)
        }
        val seen = mutableMapOf<Long, String>()
        for ((backend, priority) in declarations) {
            if (priority == null) continue
            val other = seen.put(priority, backend)
            require(other == null) {
                "available: priority $priority is declared by both '$other' and " +
                    "'$backend'; equal priorities are a configuration error (design 2.8.A)"
            }
        }
        return declarations
            .sortedWith(
                compareBy<Pair<String, Long?>> { it.second == null }
                    .thenBy { it.second ?: 0L }
                    .thenBy { it.first },
            )
            .map { it.first }
    }

    /**
     * The submenu default for [available]: the entry the user has NOT overridden,
     * i.e. the first entry of [orderedBackends]; null when nothing is declared.
     */
    fun defaultBackend(available: Map<*, *>?): String? = orderedBackends(available).firstOrNull()

    /**
     * The backend a document must select, from ONE authority.
     *
     * Design 2.9 / U17: the `available` declaration IS the selection surface, so
     * when the user has set no preference the backend is DERIVED from it (via
     * [orderedBackends], so `priority` decides and ties stay fail-closed). An
     * explicit [explicit] preference still wins (design 1-prime). Callers that
     * hold the RUNTIME projection (which drops `available`) are covered too: the
     * runtime carrier then names the selected backend, and `backend.kind` is the
     * last resort. Null = nothing to derive from, so the caller keeps its default.
     */
    fun selectedBackend(profile: Map<*, *>?, explicit: String?): String? {
        if (explicit != null) return explicit
        val available = profile?.get("available") as? Map<*, *>
        if (available != null && available.isNotEmpty()) return defaultBackend(available)
        val backend = profile?.get("backend") as? Map<*, *> ?: return null
        val carrier = backend[ProfileLayout.QueueSelectionKey] as? Map<*, *>
        val carried = carrier?.keys?.filterIsInstance<String>().orEmpty()
        if (carried.size == 1) return carried.first()
        return backend["kind"] as? String
    }

    /**
     * The declared priority of one `available.<backend>` declaration, or null when
     * it declares none. Accepts the normalized Long and the raw HOCON Int alike so
     * callers may pass a parsed or a canonical map.
     */
    fun priorityOf(backend: String, declaration: Any?): Long? {
        val map = declaration as? Map<*, *> ?: return null
        val raw = map[Key] ?: return null
        val number = raw as? Number
        require(number != null && number.toDouble() % 1.0 == 0.0) {
            "available.$backend.$Key: must be a positive integer: $raw"
        }
        val value = number.toLong()
        require(value > 0) { "available.$backend.$Key: must be a positive integer: $raw" }
        return value
    }
}
