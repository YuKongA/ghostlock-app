package com.ghostlock.app.data.route

/**
 * Semantic route selector, mirroring the native `kRoute*` wire values.
 * `0` (native `kRouteAuto`) is intentionally absent: a resolved profile must
 * declare its route explicitly.
 */
internal enum class RouteKind(
    val wire: UInt,
    val token: String,
    private val empty: RouteConfig,
    private val builder: ((String) -> Long) -> RouteConfig,
) {
    TCP_ZEROCOPY(1u, "tcp_zerocopy", TcpConfig.EMPTY, { value -> TcpConfig.from(value) }),
    SELECT_STACK(2u, "select_stack", SelectConfig.EMPTY, { value -> SelectConfig.from(value) }),
    MULTICAST_WAITER(3u, "multicast_waiter", MulticastConfig.EMPTY, { value ->
        MulticastConfig.from(value)
    }),
    ;

    fun emptyConfig(): RouteConfig = empty

    fun buildConfig(value: (String) -> Long): RouteConfig = builder(value)

    companion object {
        fun fromToken(token: String?): RouteKind? = values().firstOrNull { it.token == token }

        fun fromWire(wire: UInt): RouteKind? = values().firstOrNull { it.wire == wire }
    }
}
