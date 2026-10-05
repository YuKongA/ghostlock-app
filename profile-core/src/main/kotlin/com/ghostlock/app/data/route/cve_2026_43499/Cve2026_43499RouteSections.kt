package com.ghostlock.app.data.route.cve_2026_43499

/**
 * S4 R2: route wire sections belong to the cve_2026_43499 backend
 * (`backend.cve_2026_43499.route.<kind>`); the cve_2026_43284 backend is
 * route-less, so it owns no route section. Route *selection/private tuning* is
 * therefore backend-private, and the wire namespace is owned here.
 *
 * The app-side [com.ghostlock.app.data.route.RouteKind] enum stays in the shared
 * route package because the profile UI (app/src/main) resolves it for every
 * backend; only 43499 has catalogued routes, so it is effectively the 43499
 * route catalog. This object is the single place that turns a route token into
 * its owner-qualified wire section.
 */
object Cve2026_43499RouteSections {
    /** The owning backend id shared with native (R2 owner-qualified paths). */
    const val OWNER: String = "backend.cve_2026_43499"

    /** Owner-qualified route section for a [RouteKind] token. */
    fun sectionNameFor(kindToken: String): String = "$OWNER.route.$kindToken"

    /** True when [section] is one of this backend's route sections. */
    fun isRouteSection(section: String): Boolean = section.startsWith("$OWNER.route.")
}
