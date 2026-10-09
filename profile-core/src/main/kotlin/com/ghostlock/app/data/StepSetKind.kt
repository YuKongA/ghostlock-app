package com.ghostlock.app.data

/**
 * StepSet ids (ADR-0004 R18), App-visible. HOCON carries the human-readable
 * token; the wire field (`backend.cve_2026_43499.steps`) carries [wire].
 */
enum class StepSetKind(val token: String, val wire: UInt, val available: Boolean = true) {
    /* B1 rename: the tokens carry the chain catalogue's own spelling (native
     * contract::kChainCatalog, exported to combination-manifest.tsv); the enum
     * CONSTANT names stay as the historical ids. */
    SELINUX_CRED("selinux_cred", 1u),
    SELINUX_CRED_SECCOMP("selinux_cred_seccomp", 2u),
    PAGE_CACHE("pagecache", 3u),
    ;

    companion object {
        /** EXACT contract-surface lookup; no trimming, no case folding. */
        fun resolve(token: String?): StepSetKind? =
            token?.let { value -> entries.firstOrNull { it.token == value } }

        /** HOCON/UI input leniency: trim + lower-case. */
        fun normalize(token: String?): String? = token?.trim()?.lowercase()

        fun fromWire(wire: UInt): StepSetKind? = entries.firstOrNull { it.wire == wire }
    }
}
