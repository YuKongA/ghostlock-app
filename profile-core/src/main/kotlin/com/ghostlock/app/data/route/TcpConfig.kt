package com.ghostlock.app.data.route

data class TcpConfig(
    val attempts: UInt,
    val armSequence: UInt,
    val postReceiveHoldIterations: UInt,
) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf(
        "tcp_attempts" to attempts.toLong(),
        "tcp_arm_sequence" to armSequence.toLong(),
        "tcp_post_receive_hold_iterations" to postReceiveHoldIterations.toLong(),
    )

    override fun apply(key: String, value: Long): RouteConfig = when (key) {
        "tcp_attempts" -> copy(attempts = value.toConfigUInt())
        "tcp_arm_sequence" -> copy(armSequence = value.toConfigUInt())
        "tcp_post_receive_hold_iterations" ->
            copy(postReceiveHoldIterations = value.toConfigUInt())
        else -> this
    }

    companion object {
        val EMPTY = TcpConfig(0u, 0u, 0u)

        fun from(value: (String) -> Long): TcpConfig = TcpConfig(
            attempts = value("execution.routes.tcp_zerocopy.attempts").toConfigUInt(),
            armSequence = value("execution.routes.tcp_zerocopy.arm_sequence").toConfigUInt(),
            postReceiveHoldIterations =
                value("execution.routes.tcp_zerocopy.post_receive_hold_iterations")
                    .toConfigUInt(),
        )
    }
}
