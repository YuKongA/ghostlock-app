package com.ghostlock.app.core.victim

/**
 * 通用 victim 组件能力位（backend-agnostic，实现批 A）。
 *
 * 通用 victim 只做进程/传输/机制能力；**业务语义（何时 spawn、采样几 pass、投票、
 * 选址、finish 判定）由使用方 backend 决定**。使用方在创建会话时声明它启用的能力集，
 * 未启用的命令必须由 [VictimSession] 具名拒绝，不得静默执行。
 *
 * @property bit 位掩码中的位（稳定 ABI，不随后续枚举重排；只允许在尾部追加）。
 * @note 阶段：批 A（task-52）；不接 Flow/JNI 生产路径。
 */
internal enum class VictimCapability(val bit: Int) {
    /** 读回 victim 当前 uid（helper `probeUid`）。 */
    PROBE_UID(1 shl 0),

    /** 允许 posix_spawn helper（Native `glk_victim_spawn`）。 */
    SPAWN_HELPER(1 shl 1),

    /** helper 内 perf self-sample（仅 syscall 机制 + raw 回写；解析在 Kotlin）。 */
    PERF_SELF_SAMPLE(1 shl 2),

    /** 在 helper 内 posix_spawn `/system/bin/sh <script>`（finish 脚本；当前 uid）。 */
    EXEC_SCRIPT(1 shl 3),

    /** rooted child 驻留（park，不杀）。 */
    PARK(1 shl 4),

    /** 正常退出命令（bye）。 */
    BYE(1 shl 5),

    /** rooted child 只 detach（不 kill/reap）。 */
    DETACH_ROOTED(1 shl 6),

    /* G04/A2 append（I1：bits 7/8/9；不重排既有 0–6；bit7 当前树无 EXEC_SCRIPT_NO_KILL
     * 占用 ⇒ 按空位使用并在此登记去向）。 */
    /** J helper 读 raw `/proc/self/status`（Kotlin 解析；≤4 KiB）。 */
    PROBE_STATUS(1 shl 7),

    /** J helper 写 self comm marker（≤15B）。 */
    PROBE_COMM_SET(1 shl 8),

    /** J helper 读 self comm（len/c0/bytes）。 */
    PROBE_COMM(1 shl 9),
}

/**
 * 能力位集合（backend 声明；不可变）。
 *
 * @property mask 位掩码（[VictimCapability.bit] 的或）。
 * @throws IllegalArgumentException 掩码含未知位（fail-closed，防手误扩权）。
 * @note 阶段：批 A。
 */
internal class VictimCapabilities private constructor(val mask: Int) {
    init {
        val known = VictimCapability.entries.fold(0) { acc, item -> acc or item.bit }
        require(mask and known.inv() == 0) { "victim-capability:unknown-bit" }
    }

    /** @param capability 待检查能力。 @return true=本集合启用该能力。 */
    fun contains(capability: VictimCapability): Boolean = mask and capability.bit != 0

    /** @return 本集合启用的能力列表（稳定枚举序）。 */
    fun enabled(): List<VictimCapability> = VictimCapability.entries.filter(::contains)

    companion object {
        /** 空集合（所有命令具名拒绝）。 */
        val NONE: VictimCapabilities = VictimCapabilities(0)

        /**
         * 由能力列表构造集合。
         *
         * @param capabilities 启用的能力。
         * @return 不可变集合。
         */
        fun of(vararg capabilities: VictimCapability): VictimCapabilities =
            VictimCapabilities(capabilities.fold(0) { acc, item -> acc or item.bit })

        /**
         * 由位掩码构造集合（与 [mask] 对拍用）。
         *
         * @param mask 位掩码。
         * @return 不可变集合。
         * @throws IllegalArgumentException 含未知位。
         */
        fun fromMask(mask: Int): VictimCapabilities = VictimCapabilities(mask)
    }
}
