package com.ghostlock.app.data.component

/**
 * App-side component model: the frontend/backend ids the wire can carry and
 * their availability. It mirrors the native authority
 * (`pipeline/component_catalog.hpp` + `frontend_contract.hpp` /
 * `backend_policy.hpp`): unknown ids are rejected at decode time, while
 * known-but-unavailable ids (cve_2026_64560 and the other pure-header
 * placeholders) decode and are rejected before the attack. The two wired
 * terminals (root_child / umh_forward) and the two implemented backends
 * (cve_2026_43499 / cve_2026_43284) are available.
 *
 * The backend enum is the App-side authority for the header backend selection
 * (native `kBackend*`); [BackendKind.available] mirrors the native catalog, so a
 * known-but-unavailable backend can be displayed but never selected or run.
 */

/**
 * Frontend component ids; wire values match native `kFrontend*`.
 *
 * The vocabulary exposes the shared two-operation contract (task-7):
 * [resolve] is an EXACT lookup for the contract surface (wire tokens, manifest
 * rows, already-canonical values), [normalize] is the trim + lower-case leniency
 * applied only at a HOCON/UI input boundary. Call sites at a boundary read
 * `resolve(normalize(value))`.
 */
enum class FrontendKind(val wire: Int, val token: String, val available: Boolean) {
    RootChild(1, "root_child", true),
    UmhForward(2, "umh_forward", true),
    ;

    companion object {
        /** EXACT contract-surface lookup; no trimming, no case folding. */
        fun resolve(token: String?): FrontendKind? =
            token?.let { value -> entries.firstOrNull { it.token == value } }

        /** HOCON/UI input leniency: trim + lower-case. */
        fun normalize(token: String?): String? = token?.trim()?.lowercase()

        fun fromWire(wire: Int): FrontendKind? = entries.firstOrNull { it.wire == wire }
    }
}

/**
 * Backend (vulnerability primitive) ids; wire values match native `kBackend*`.
 * [available] is the App mirror of native `backend_available`; only an available
 * backend may be selected for a run.
 */
enum class BackendKind(val wire: Int, val token: String, val available: Boolean) {
    Cve2026_43499(1, "cve_2026_43499", true),
    Cve2026_64560(2, "cve_2026_64560", false),
    /* Pure-header placeholders: declared in the native catalog, displayable,
     * never selectable (VocabularyCatalog.available mirrors native). */
    Cve2026_31431(3, "cve_2026_31431", false),
    Cve2026_43503(4, "cve_2026_43503", false),
    Cve2026_23274(5, "cve_2026_23274", false),
    Cve2026_43284(6, "cve_2026_43284", true),
    ;

    companion object {
        /** Wire default, matching the native `kBackendCve202643499` default. */
        val Default = Cve2026_43499

        fun fromWire(wire: Int): BackendKind? = entries.firstOrNull { it.wire == wire }

        /** EXACT contract-surface lookup; no trimming, no case folding. */
        fun resolve(token: String?): BackendKind? =
            token?.let { value -> entries.firstOrNull { it.token == value } }

        /** HOCON/UI input leniency: trim + lower-case. */
        fun normalize(token: String?): String? = token?.trim()?.lowercase()

        /**
         * Parses a persisted selection in any historical spelling: the canonical
         * token, the enum name, or a numeric wire id (legacy ordinal). Unknown,
         * empty and non-numeric unknown values yield null so the caller can fall
         * back instead of selecting a dead backend.
         */
        fun fromStored(value: String?): BackendKind? {
            /* A persisted preference is an input boundary, so the stored
             * spelling is normalized once and then resolved exactly. */
            val raw = normalize(value)?.takeIf { it.isNotEmpty() } ?: return null
            val wire = raw.toIntOrNull()
            return resolve(raw)
                ?: wire?.let(::fromWire)
                ?: entries.firstOrNull { it.name.equals(raw, ignoreCase = true) }
        }

        /** Resolves a selection to a usable backend, falling back to [Default]. */
        fun selectableOrFallback(kind: BackendKind?): BackendKind =
            kind?.takeIf { it.available } ?: Default
    }
}

/** Availability is owned here for the App, mirroring the native catalog. */
object ComponentAvailability {
    fun frontendAvailable(kind: FrontendKind): Boolean = kind.available
    fun backendAvailable(kind: BackendKind): Boolean = kind.available
}
