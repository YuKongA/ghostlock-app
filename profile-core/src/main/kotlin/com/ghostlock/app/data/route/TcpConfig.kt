package com.ghostlock.app.data.route

/**
 * `route.tcp_zerocopy` section. Native declares these three as plain (always
 * present) fields, so they stay non-null and are always emitted. The optional
 * payload-placement geometry mirrors native's `geometry.tcp_*` optionals: it
 * is part of the route section's key universe (and so of the native field
 * manifest), but a profile that does not carry it omits the key entirely.
 */
data class TcpConfig(
    val attempts: UInt,
    val armSequence: UInt,
    val postReceiveHoldIterations: UInt,
    val payloadDelta: Long? = null,
    val chunkBias: ULong? = null,
    val fakeTaskOff: ULong? = null,
    val credCopyOff: ULong? = null,
) : RouteConfig {
    override fun entries(): List<Pair<String, ULong>> = buildList {
        add("attempts" to attempts.toULong())
        add("arm_sequence" to armSequence.toULong())
        add("post_receive_hold_iterations" to postReceiveHoldIterations.toULong())
        payloadDelta?.let { add("payload_delta" to it.toULong()) }
        chunkBias?.let { add("chunk_bias" to it) }
        fakeTaskOff?.let { add("fake_task_off" to it) }
        credCopyOff?.let { add("cred_copy_off" to it) }
    }

    override fun apply(key: String, value: ULong): RouteConfig = when (key) {
        "attempts" -> copy(attempts = value.toUInt())
        "arm_sequence" -> copy(armSequence = value.toUInt())
        "post_receive_hold_iterations" -> copy(postReceiveHoldIterations = value.toUInt())
        "payload_delta" -> copy(payloadDelta = value.toLong())
        "chunk_bias" -> copy(chunkBias = value)
        "fake_task_off" -> copy(fakeTaskOff = value)
        "cred_copy_off" -> copy(credCopyOff = value)
        else -> this
    }

    companion object {
        val EMPTY = TcpConfig(0u, 0u, 0u)

        fun from(value: (String) -> Long?): TcpConfig = TcpConfig(
            attempts = value("execution.routes.tcp_zerocopy.attempts")?.toUInt() ?: 0u,
            armSequence = value("execution.routes.tcp_zerocopy.arm_sequence")?.toUInt() ?: 0u,
            postReceiveHoldIterations =
                value("execution.routes.tcp_zerocopy.post_receive_hold_iterations")?.toUInt() ?: 0u,
        )
    }
}
