package com.ghostlock.app.data.route

internal data class TcpConfig(
    val attempts: Long,
    val armSequence: Long,
    val postReceiveHoldIterations: Long,
) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf(
        "tcp_attempts" to attempts,
        "tcp_arm_sequence" to armSequence,
        "tcp_post_receive_hold_iterations" to postReceiveHoldIterations,
    )

    override fun apply(key: String, value: Long): RouteConfig = when (key) {
        "tcp_attempts" -> copy(attempts = value)
        "tcp_arm_sequence" -> copy(armSequence = value)
        "tcp_post_receive_hold_iterations" -> copy(postReceiveHoldIterations = value)
        else -> this
    }

    companion object {
        val EMPTY = TcpConfig(0L, 0L, 0L)

        fun from(value: (String) -> Long): TcpConfig = TcpConfig(
            attempts = value("execution.routes.tcp_zerocopy.attempts"),
            armSequence = value("execution.routes.tcp_zerocopy.arm_sequence"),
            postReceiveHoldIterations =
                value("execution.routes.tcp_zerocopy.post_receive_hold_iterations"),
        )
    }
}
