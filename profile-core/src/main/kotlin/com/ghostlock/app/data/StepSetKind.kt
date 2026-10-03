package com.ghostlock.app.data

/**
 * StepSet ids (ADR-0004 R18), App-visible. HOCON carries the human-readable
 * token; the wire field (`backend.cve_2026_43499.steps`) carries [wire].
 */
enum class StepSetKind(val token: String, val wire: UInt) {
    W1W2("w1_w2", 1u),
    W1W3("w1_w3", 2u),
    PAGE_CACHE_WRITE("pagecache_write", 3u),
    ;

    companion object {
        fun fromToken(token: String?): StepSetKind? =
            entries.firstOrNull { it.token == token?.trim()?.lowercase() }

        fun fromWire(wire: UInt): StepSetKind? = entries.firstOrNull { it.wire == wire }
    }
}
