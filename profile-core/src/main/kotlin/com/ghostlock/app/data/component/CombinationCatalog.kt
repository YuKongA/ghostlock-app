package com.ghostlock.app.data.component

import com.ghostlock.app.data.StepSetKind
import com.ghostlock.app.data.route.RouteKind

/**
 * F4 combination-token specification (ADR-0006 T5 / S4 R6b).
 *
 * The user-visible selection is exactly ONE token carried in
 * `backend.<id>.steps` (a string). A token is `<route>_<path>` for a backend
 * with a route axis (cve_2026_43499) and a bare `<path>` for a backend without
 * one (cve_2026_43284). The token DERIVES the internal (route, step set,
 * terminal/path) triple, which is no longer independently selectable;
 * [StepSetKind] survives only as that derived vocabulary.
 *
 * Instances are never written by hand: every row is parsed from the
 * native-exported `combination-manifest.tsv` (see [CombinationCatalog]), whose
 * contents are asserted against `contract::kCombinationCatalog` by the native
 * `combination_manifest_test`. `available` = false is a PLANNED item: it
 * parses, but the selection gate rejects it and the dropdown greys it out.
 */
data class CombinationSpec(
    /** The single user-visible selection token (lowercase, `_` separated). */
    val token: String,
    /** Owning backend; the token lives under this backend's `.steps`. */
    val backend: BackendKind,
    /** Derived geometry route; null for a backend without a route axis. */
    val route: RouteKind?,
    /** Token `<path>` component (rootchild / shizuku / umh). */
    val path: String,
    /** Derived backend step set (ADR-0004 R18). */
    val steps: StepSetKind,
    /** Derived terminal / handoff path. */
    val terminal: FrontendKind,
    /** False = registered but planned; parses and displays, cannot be selected. */
    val available: Boolean,
    /** Human-readable row summary the dropdown shows (native-exported doc). */
    val doc: String,
) {
    /** True for a backend whose token carries a `<route>_` prefix. */
    val hasRouteAxis: Boolean get() = route != null
}

/**
 * F4 single Kotlin authority for the combination whitelist: the native-exported
 * `combination-manifest.tsv` (runtime resource, regenerated with
 * `make -C src combination-manifest`), parsed once and never hard-coded here.
 *
 * Two deliberately separate operations guard the cross-language contract:
 *
 *  - [resolve] is an EXACT byte match, the same judgement as native
 *    `contract::combination_resolve` (src/core/contract/identity.hpp). It is the
 *    contract surface: the wire, the persisted selection and any already
 *    canonical token go through it, and a token native would reject (padded,
 *    upper-cased, unknown) resolves to null here too.
 *  - [normalize] is the trim + lower-case leniency applied ONLY at the HOCON /
 *    UI input boundary ([com.ghostlock.app.data.ProfileLayout] and the settings
 *    input). A normalized value must still resolve; callers that write a token
 *    to the wire write the resolved canonical token, never the user's spelling.
 *
 * Catalogue order is manifest order (the dropdown order and [defaultSpec]
 * depend on it, so the manifest row order is significant). A malformed or
 * missing row fails closed at load time instead of reaching the selection.
 */
object CombinationCatalog {
    /** The native-exported combination catalogue, on the runtime classpath. */
    const val MANIFEST_RESOURCE: String = "combination-manifest.tsv"

    /** Manifest columns: token, backend, route, path, steps, terminal, available, doc. */
    private const val MANIFEST_COLUMNS = 8

    /** Manifest `route` value for a backend without a route axis. */
    private const val ROUTE_NONE = "none"

    /** Catalogue rows in manifest order; parsed once. */
    val specs: List<CombinationSpec> by lazy {
        val stream = CombinationCatalog::class.java.classLoader
            ?.getResourceAsStream(MANIFEST_RESOURCE)
            ?: error("missing combination manifest resource: $MANIFEST_RESOURCE")
        parseManifest(stream.bufferedReader().use { it.readText() })
    }

    /** Token -> row. Tokens are globally unique (native asserts the same). */
    val byToken: Map<String, CombinationSpec> by lazy { specs.associateBy { it.token } }

    /** The app default: the first available token of cve_2026_43499. */
    val defaultSpec: CombinationSpec by lazy {
        specs.firstOrNull { it.available && it.backend == BackendKind.Cve2026_43499 }
            ?: specs.first()
    }

    /**
     * Parses manifest text into catalogue rows, in file order. Shared by the
     * runtime loader and the agreement test, which parses the app test resource
     * with this same function and compares it with [specs] (the runtime
     * resource), so the two committed copies cannot drift.
     *
     * Structural consistency is checked EXPLICITLY against the exported columns
     * (never inferred from the token spelling): a row without a route axis
     * (`route == none`) must carry the bare `<path>` token, a row with a route
     * axis must carry exactly `<route>_<path>`.
     */
    fun parseManifest(text: String): List<CombinationSpec> {
        val out = mutableListOf<CombinationSpec>()
        val seen = mutableSetOf<String>()
        for (raw in text.lineSequence()) {
            val line = raw.trimEnd('\r')
            if (line.isBlank() || line.startsWith("#")) continue
            val parts = line.split('\t')
            require(parts.size == MANIFEST_COLUMNS) {
                "combination manifest line needs $MANIFEST_COLUMNS tab-separated columns: $line"
            }
            val token = parts[0]
            require(token.isNotBlank()) { "combination manifest token is empty: $line" }
            require(seen.add(token)) { "duplicate combination token: $token" }
            val backend = BackendKind.resolve(parts[1])
                ?: error("unknown backend in combination manifest: $line")
            val route = if (parts[2] == ROUTE_NONE) {
                null
            } else {
                RouteKind.resolve(parts[2])
                    ?: error("unknown route in combination manifest: $line")
            }
            val path = parts[3]
            require(path.isNotBlank()) { "combination manifest path is empty: $line" }
            /* Structural check against the exported columns only. The route
             * PREFIX is an abbreviation owned by the native table (mcast /
             * pselect / tcp for multicast_waiter / select_stack / tcp_zerocopy),
             * so it is not derived here: without a route axis the token is the
             * bare <path>, with one it must be <prefix>_<path>. The prefix ->
             * route pairing itself is asserted row by row in
             * CombinationTokenAgreementTest and natively. */
            if (route == null) {
                require(token == path) {
                    "combination manifest token without a route axis must be the bare path: $line"
                }
            } else {
                val suffix = "_" + path
                require(token.length > suffix.length && token.endsWith(suffix)) {
                    "combination manifest token with a route axis must be <prefix>_<path>: $line"
                }
            }
            val steps = StepSetKind.resolve(parts[4])
                ?: error("unknown step set in combination manifest: $line")
            val terminal = FrontendKind.resolve(parts[5])
                ?: error("unknown terminal in combination manifest: $line")
            val available = when (parts[6]) {
                "1" -> true
                "0" -> false
                else -> error("combination manifest available must be 1/0: $line")
            }
            out += CombinationSpec(
                token = token,
                backend = backend,
                route = route,
                path = path,
                steps = steps,
                terminal = terminal,
                available = available,
                doc = parts[7],
            )
        }
        require(out.isNotEmpty()) { "combination manifest is empty" }
        return out
    }

    /**
     * Resolves a token in the context of one backend. EXACT match: the token is
     * compared byte for byte, mirroring native `combination_resolve`. Apply
     * [normalize] first at a HOCON/UI input boundary.
     */
    fun resolve(backend: BackendKind, token: String?): CombinationSpec? {
        if (token == null) return null
        val spec = byToken[token] ?: return null
        return if (spec.backend == backend) spec else null
    }

    /** Resolves a globally unique token. EXACT match; see [resolve]. */
    fun resolve(token: String?): CombinationSpec? = token?.let { byToken[it] }

    /**
     * The HOCON/UI input leniency: trim surrounding whitespace and lower-case.
     * Returns null for null; an empty or unknown result still fails [resolve].
     */
    fun normalize(token: String?): String? = token?.trim()?.lowercase()

    /** Every token owned by [backend], catalogue order. */
    fun forBackend(backend: BackendKind): List<CombinationSpec> =
        specs.filter { it.backend == backend }

    /** Wired/selectable tokens only, catalogue order. */
    fun availableForBackend(backend: BackendKind): List<CombinationSpec> =
        forBackend(backend).filter { it.available }

    /** First selectable token of [backend], or null when none is wired. */
    fun defaultFor(backend: BackendKind): CombinationSpec? =
        availableForBackend(backend).firstOrNull() ?: forBackend(backend).firstOrNull()

    /**
     * The token recommended for [backend]: the first available token whose
     * derived route matches [route], falling back to the first available token
     * of the backend when the route is unknown or pins none.
     */
    fun recommended(backend: BackendKind, route: RouteKind?): CombinationSpec? {
        val available = availableForBackend(backend)
        return available.firstOrNull { route != null && it.route == route }
            ?: available.firstOrNull()
    }

    /**
     * Legacy HOCON step-set migration: maps an old step id
     * (`w1_w2`/`w1_w3`/`pagecache_write`) plus the profile's route onto the
     * equivalent combination token. This is a HOCON input boundary, so the value
     * is normalized before it is resolved; a value that already is a combination
     * token resolves verbatim, which keeps re-parsing idempotent. A route with no
     * matching token fails closed (null).
     */
    fun fromLegacySteps(
        backend: BackendKind,
        stepsToken: String?,
        route: RouteKind?,
    ): CombinationSpec? {
        val normalized = normalize(stepsToken) ?: return null
        resolve(backend, normalized)?.let { return it }
        if (backend == BackendKind.Cve2026_43284) {
            return if (normalized == StepSetKind.PAGE_CACHE_WRITE.token) {
                fromDerived(backend, StepSetKind.PAGE_CACHE_WRITE, FrontendKind.UmhForward)
            } else {
                null
            }
        }
        if (backend != BackendKind.Cve2026_43499) return null
        val derivedSteps = when (normalized) {
            StepSetKind.W1W2.token -> StepSetKind.W1W2
            StepSetKind.W1W3.token -> StepSetKind.W1W3
            else -> return null
        }
        if (route == null) return null
        /* Both shizuku and rootchild tokens use the root_child terminal, so
         * the derived step set is the discriminator. */
        return specs.firstOrNull {
            it.backend == backend && it.route == route && it.steps == derivedSteps
        }
    }

    /** Maps a derived (backend, steps, terminal) triple back to its token. */
    fun fromDerived(
        backend: BackendKind,
        steps: StepSetKind,
        terminal: FrontendKind,
    ): CombinationSpec? = specs.firstOrNull {
        it.backend == backend && it.steps == steps && it.terminal == terminal
    }
}
