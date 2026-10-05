package com.ghostlock.app.data.route

/**
 * Semantic route selector, mirroring the native `kRoute*` wire values.
 * `0` (native `kRouteAuto`) is intentionally absent: a resolved profile must
 * declare its route explicitly.
 */
enum class RouteKind(
    val wire: UInt,
    val token: String,
    private val empty: RouteConfig,
    private val builder: ((String) -> Long?) -> RouteConfig,
    /** Mirror of the native route catalogue's availability (task-6 manifest). */
    val available: Boolean = true,
) {
    TCP_ZEROCOPY(1u, "tcp_zerocopy", TcpConfig.EMPTY, { value -> TcpConfig.from(value) }),
    SELECT_STACK(2u, "select_stack", SelectConfig.EMPTY, { value -> SelectConfig.from(value) }),
    MULTICAST_WAITER(3u, "multicast_waiter", MulticastConfig.EMPTY, { value ->
        MulticastConfig.from(value)
    }),
    ;

    fun emptyConfig(): RouteConfig = empty

    fun buildConfig(value: (String) -> Long?): RouteConfig = builder(value)

    companion object {
        /** EXACT contract-surface lookup; no trimming, no case folding. */
        fun resolve(token: String?): RouteKind? =
            token?.let { value -> entries.firstOrNull { it.token == value } }

        /** HOCON/UI input leniency: trim + lower-case. */
        fun normalize(token: String?): String? = token?.trim()?.lowercase()

        fun fromWire(wire: UInt): RouteKind? = entries.firstOrNull { it.wire == wire }
    }
}
