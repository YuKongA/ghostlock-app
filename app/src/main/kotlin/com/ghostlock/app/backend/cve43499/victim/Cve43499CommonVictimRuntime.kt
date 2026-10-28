package com.ghostlock.app.backend.cve43499.victim

import com.ghostlock.app.core.victim.VictimNative
import com.ghostlock.app.core.victim.VictimNativeJni
import com.ghostlock.app.core.victim.VictimProtocol
import com.ghostlock.app.core.victim.VictimSessionConfig

/**
 * A1/A2a（task-86）：公共 helper victim runtime（core `VictimSession` + `VictimNativeJni`）。
 *
 * - 用 [Cve43499VictimLaunchSpecFactory] 的 `VictimSessionConfig` 创建会话（argv/env 空、
 *   capability 符号位、rawSize 由 perf 派生）；
 * - 只负责创建/启动载体；Flow 生命周期语义不变（`injection.victim=common` 时作为
 *   `victimFactory`；process/jni-spawn 路径**不删/不禁用**）；
 * - **43499 不使用** 98260 的 script-detach payload 载体；43499 helper
 *   是长驻 victim（Q5 事实，文档登记）。
 *
 * @param helperPath `nativeLibraryDir/libghostlock_victim_helper.so` 绝对路径。
 * @param rawSizeBytes raw memfd 容量（perf 派生；无 perf 时 `VictimProtocol.RAW_HEADER_BYTES`）。
 * @param native Native transport seam（生产=`VictimNativeJni`；宿主 fake 可注入）。
 * @note 阶段：A1/A2a provisional；按用户令未编译/未跑单测；真机 UNVERIFIED。
 */
internal class Cve43499CommonVictimRuntime(
    private val helperPath: String,
    private val rawSizeBytes: Long = VictimProtocol.RAW_HEADER_BYTES.toLong(),
    private val native: VictimNative = VictimNativeJni,
    /* task-91：A1 killPolicy（D0 冻结后映射公共 spec）。 */
    private val killPolicy: Cve43499VictimKillPolicy = Cve43499VictimKillPolicy.DEFAULT,
) {
    private val delegate: Cve43499JniVictimRuntime =
        Cve43499JniVictimRuntime(helperPath = helperPath, rawSizeBytes = rawSizeBytes)

    /** @return 由公共 config 创建的会话（backend `VictimSession` 适配；未启动）。 */
    fun newVictimSession(): VictimSession {
        val config: VictimSessionConfig = Cve43499VictimLaunchSpecFactory.create(
            helperPath = helperPath,
            rawSizeBytes = rawSizeBytes,
        )
        return Cve43499VictimAdapter(
            runtime = delegate,
            native = native,
            helperPath = helperPath,
            rawSizeBytes = rawSizeBytes,
            sessionConfig = config,
            killPolicy = killPolicy,
        )
    }

    /** @return helper 采样口（perf 路径用）；未启动/无 perf 返回 null。 */
    fun currentSampler(): Cve43499VictimPerfSampler? = delegate.currentSampler()

    /** @return 本 runtime 的 killPolicy（A1 证据/测试用）。 */
    internal fun killPolicy(): Cve43499VictimKillPolicy = killPolicy

    /** 释放会话（幂等；cleanup 兜底）。 */
    fun dispose() = delegate.dispose()
}
