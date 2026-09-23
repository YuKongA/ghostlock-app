package com.ghostlock.app.data.route

/**
 * Semantic route selector, mirroring the native `kRoute*` wire values.
 * `0` (native `kRouteAuto`) is intentionally absent: a resolved profile must
 * declare its route explicitly.
 */
internal enum class RouteKind(
    val wire: Int,
    val token: String,
    private val empty: RouteConfig,
    private val builder: ((String) -> Long) -> RouteConfig,
) {
    TCP_ZEROCOPY(1, "tcp_zerocopy", TcpConfig.EMPTY, { value -> TcpConfig.from(value) }),
    SELECT_STACK(2, "select_stack", SelectConfig.EMPTY, { value -> SelectConfig.from(value) }),
    MULTICAST_WAITER(3, "multicast_waiter", MulticastConfig.EMPTY, { value ->
        MulticastConfig.from(value)
    }),
    ;

    fun emptyConfig(): RouteConfig = empty

    fun buildConfig(value: (String) -> Long): RouteConfig = builder(value)

    companion object {
        fun fromToken(token: String?): RouteKind? = values().firstOrNull { it.token == token }

        fun fromWire(wire: Int): RouteKind? = values().firstOrNull { it.wire == wire }
    }
}
