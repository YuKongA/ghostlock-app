package com.ghostlock.app.backend.backends.cve43499.victim

import com.ghostlock.app.core.victim.VictimCapabilities
import com.ghostlock.app.core.victim.VictimCapability
import com.ghostlock.app.core.victim.VictimSessionConfig

/**
 * A1/A2a（task-86）：为公共 helper victim 产出 `VictimSessionConfig`（backend 负责符号/策略，
 * core 不引入 43499 常量）。
 *
 * @note argv/env 空（公共 transport 按 helperPath 自解析）；capabilities 用**符号**位
 *   （PROBE_UID/SPAWN_HELPER/PERF_SELF_SAMPLE/EXEC_SCRIPT/PARK/BYE/DETACH_ROOTED/
 *   PROBE_STATUS/PROBE_COMM_SET/PROBE_COMM），不写裸数字。
 * @note 43499 **不使用** legacy script 载体（script-detach payload 载体）；
 *   43499 helper 是长驻 victim，走公共 `VictimSession`/本 config（Q5 事实）。
 * @note 阶段：A1/A2a provisional；按用户令未编译/未跑单测，真机 UNVERIFIED。
 */
internal object Cve43499VictimLaunchSpecFactory {
    /** 43499 helper 声明启用的能力（符号位；与 helper capability enum 对拍）。 */
    val COMMON_CAPABILITIES: List<VictimCapability> = listOf(
        VictimCapability.PROBE_UID,
        VictimCapability.SPAWN_HELPER,
        VictimCapability.PERF_SELF_SAMPLE,
        VictimCapability.EXEC_SCRIPT,
        VictimCapability.PARK,
        VictimCapability.BYE,
        VictimCapability.DETACH_ROOTED,
        VictimCapability.PROBE_STATUS,
        VictimCapability.PROBE_COMM_SET,
        VictimCapability.PROBE_COMM,
    )

    /**
     * 创建公共 victim 启动规格。
     *
     * @param helperPath `nativeLibraryDir/libghostlock_victim_helper.so` 绝对路径。
     * @param rawSizeBytes raw memfd 容量（由 perf 派生；无 perf 时用协议头最小容量）。
     * @param controlTimeoutMs 控制帧基础超时（既有 limits）。
     * @param commandTimeoutMs 机制命令额外上限（既有 limits）。
     * @param closeTimeoutMs 回收上限（既有 limits）。
     * @return 不可变 [VictimSessionConfig]；argv/env 为空。
     */
    fun create(
        helperPath: String,
        rawSizeBytes: Long,
        /* task-91：killPolicy 随 spec 携带（D0 前由 backend runtime/adapter 持有）。 */
        policy: Cve43499VictimKillPolicy = Cve43499VictimKillPolicy.DEFAULT,
        controlTimeoutMs: Long = Cve43499JniVictimLimits.CONTROL_TIMEOUT_MS,
        commandTimeoutMs: Long = Cve43499JniVictimLimits.COMMAND_TIMEOUT_MS,
        closeTimeoutMs: Long = Cve43499JniVictimLimits.CLOSE_TIMEOUT_MS,
    ): VictimSessionConfig = VictimSessionConfig(
        helperPath = helperPath,
        helperArgv = listOf(helperPath),
        helperEnv = emptyList(),
        capabilities = VictimCapabilities.of(*COMMON_CAPABILITIES.toTypedArray()),
        rawSizeBytes = rawSizeBytes,
        controlTimeoutMs = controlTimeoutMs,
        commandTimeoutMs = commandTimeoutMs,
        closeTimeoutMs = closeTimeoutMs,
    )

    /** @return 本次 spec 使用的 killPolicy（D0 冻结后映射公共 spec 字段）。 */
    fun policyOf(policy: Cve43499VictimKillPolicy = Cve43499VictimKillPolicy.DEFAULT):
        Cve43499VictimKillPolicy = policy
}
