package com.ghostlock.app.data.component

/**
 * Batch 4 (D3) App-side component model placeholder: the frontend/backend ids
 * the wire can carry and their availability. It mirrors the native authority
 * (`route/component_catalog.hpp` + `frontend_contract.hpp` / `backend_policy.hpp`):
 * unknown ids are rejected at decode time, while known-but-unavailable ids
 * (umh_forward / cve_2026_64560) decode and are rejected before the attack.
 *
 * No UI is exposed for this yet; the model exists so the App can express
 * selection/availability without changing current behavior.
 */

/** Frontend component ids; wire values match native `kFrontend*`. */
enum class FrontendKind(val wire: Int, val token: String) {
    RootChild(1, "root_child"),
    UmhForward(2, "umh_forward"),
}

/** Backend (vulnerability primitive) ids; wire values match native `kBackend*`. */
enum class BackendKind(val wire: Int, val token: String) {
    Cve2026_43499(1, "cve_2026_43499"),
    Cve2026_64560(2, "cve_2026_64560"),
}

/** Availability is owned here for the App, mirroring the native catalog. */
object ComponentAvailability {
    fun frontendAvailable(kind: FrontendKind): Boolean = kind == FrontendKind.RootChild
    fun backendAvailable(kind: BackendKind): Boolean = kind == BackendKind.Cve2026_43499
}
