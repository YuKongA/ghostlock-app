package com.ghostlock.app.data.component

/**
 * App-side component model: the frontend/backend ids the wire can carry and
 * their availability. It mirrors the native authority
 * (`pipeline/component_catalog.hpp` + `frontend_contract.hpp` /
 * `backend_policy.hpp`): unknown ids are rejected at decode time, while
 * known-but-unavailable ids (umh_forward / cve_2026_64560 / cve_2026_43284)
 * decode and are rejected before the attack.
 *
 * The backend enum is the App-side authority for the header backend selection
 * (native `kBackend*`); [BackendKind.available] mirrors the native catalog, so a
 * known-but-unavailable backend can be displayed but never selected or run.
 */

/** Frontend component ids; wire values match native `kFrontend*`. */
enum class FrontendKind(val wire: Int, val token: String) {
    RootChild(1, "root_child"),
    UmhForward(2, "umh_forward"),
}

/**
 * Backend (vulnerability primitive) ids; wire values match native `kBackend*`.
 * [available] is the App mirror of native `backend_available`; only an available
 * backend may be selected for a run.
 */
enum class BackendKind(val wire: Int, val token: String, val available: Boolean) {
    Cve2026_43499(1, "cve_2026_43499", true),
    Cve2026_64560(2, "cve_2026_64560", false),
    Cve2026_43284(6, "cve_2026_43284", false),
    ;

    companion object {
        /** Wire default, matching the native `kBackendCve202643499` default. */
        val Default = Cve2026_43499

        fun fromWire(wire: Int): BackendKind? = entries.firstOrNull { it.wire == wire }

        fun fromToken(token: String?): BackendKind? {
            val normalized = token?.trim()?.lowercase() ?: return null
            return entries.firstOrNull { it.token == normalized }
        }

        /**
         * Parses a persisted selection in any historical spelling: the canonical
         * token, the enum name, or a numeric wire id (legacy ordinal). Unknown,
         * empty and non-numeric unknown values yield null so the caller can fall
         * back instead of selecting a dead backend.
         */
        fun fromStored(value: String?): BackendKind? {
            val raw = value?.trim()?.takeIf { it.isNotEmpty() } ?: return null
            val wire = raw.toIntOrNull()
            return fromToken(raw)
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
    fun frontendAvailable(kind: FrontendKind): Boolean = kind == FrontendKind.RootChild
    fun backendAvailable(kind: BackendKind): Boolean = kind.available
}
