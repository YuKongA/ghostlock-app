package com.ghostlock.app.backend.cve43499.flow

/**
 * 43499 的三条 route 私有参数。
 *
 * 权威是 `attack_contract_fields.def` 的 Init43499 route 段，加上 legacy
 * `route/expected_keys.hpp` 的 route 闭集。会话级 route 是唯一权威：steps
 * 不允许再带 route；同一 run 只能属于一个 route 家族。
 *
 * @property wireId native RouteId（tcp=1，select=2，multicast=3）。
 * @property token Kotlin/日志身份，不进入 native。
 */
sealed interface Flow43499Route {
    val wireId: Long
    val token: String

    /**
     * native Init 的三个 present 位：bit1=tcp、bit2=select、bit4=multicast。
     *
     * wire 顺序是 multicastPresent、tcpPresent、selectPresent，不是掩码位序；
     * 编码器必须逐位展开，并对 wireId==0（Unset）直接拒绝。
     */
    val presentMask: Int
        get() = when (this) {
            is TcpZerocopy -> 1
            is SelectStack -> 2
            is MulticastWaiter -> 4
        }

    /**
     * `tcp_zerocopy`：native Init 字段 + legacy route 五键。
     *
     * @property zeroCopyMapBytes native Init 的 zerocopy 映射长度。
     * @property postReceiveHoldIterations legacy route 键（uint）。
     * @property payloadDelta legacy route 键（int，可为负）。
     * @property chunkBias legacy route 键（uint）。
     * @property fakeTaskOff legacy route 键（uint）。
     * @property credCopyOff legacy route 键（uint）。
     */
    data class TcpZerocopy(
        val zeroCopyMapBytes: Long = 0L,
        val postReceiveHoldIterations: Long? = null,
        val payloadDelta: Int? = null,
        val chunkBias: Long? = null,
        val fakeTaskOff: Long? = null,
        val credCopyOff: Long? = null,
    ) : Flow43499Route {
        override val wireId: Long get() = 1L
        override val token: String get() = "tcp_zerocopy"
    }

    /**
     * `select_stack`：native Init 字段 + legacy route 四键。
     *
     * @property fdCount native Init 的 fd 数。
     * @property waiterShift legacy route 键（int，可为负）。
     * @property enterDelayUs legacy route 键（uint）。
     * @property timeoutUs legacy route 键（uint）。
     * @property compactWaiter legacy route 键（bool）。
     */
    data class SelectStack(
        val fdCount: Long = 0L,
        val waiterShift: Int? = null,
        val enterDelayUs: Long? = null,
        val timeoutUs: Long? = null,
        val compactWaiter: Boolean? = null,
    ) : Flow43499Route {
        override val wireId: Long get() = 2L
        override val token: String get() = "select_stack"
    }

    /**
     * `multicast_waiter`：native Init 字段 + legacy 的 waiter_off。
     *
     * attempts/arm_sequence/arm_hold/buffer_size/task_offset/lock_offset 已由
     * Init 的 limits/stamp 持有，这里不再重复，避免同名双权威。
     *
     * @property armRounds native Init 的 arm 轮数上限。
     * @property stampWaiterShift native Init 的 stamp waiter 位移。
     * @property waiterOff legacy route 键（int，可为负）。
     */
    data class MulticastWaiter(
        val armRounds: Long = 0L,
        val stampWaiterShift: Long = 0L,
        val waiterOff: Int? = null,
    ) : Flow43499Route {
        override val wireId: Long get() = 3L
        override val token: String get() = "multicast_waiter"
    }
}
