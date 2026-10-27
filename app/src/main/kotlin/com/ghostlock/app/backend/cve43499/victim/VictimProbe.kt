package com.ghostlock.app.backend.cve43499.victim

/**
 * 受害者 uid 读回结果（自包含；不依赖已删除的 `engine.ktstep` 包）。
 *
 * @note 只表达「Kotlin 子进程读回了什么」，不表达内核写是否成功；业务成功判定在
 *   `finish/FinishAcceptance.kt` 的具名验收里。
 */
internal sealed interface VictimUidResult {
    /** 读回了一个 uid 数值。 */
    data class Ok(val uid: Long) : VictimUidResult

    /** 读回失败；reason 是稳定可检索的具名文本（例如 `victim-probe:epipe`）。 */
    data class Failed(val reason: String) : VictimUidResult
}

/**
 * victim 身份证据：PID + `/proc/<pid>/stat` starttime + uid。
 *
 * @property pid 子进程 PID。
 * @property startTimeTicks `/proc/<pid>/stat` 第 22 字段（boot 起的 tick 数）；用于
 *   在 execute 之后复查「同一进程仍在」，防止 PID 被复用。
 * @property uid 读回的 uid。
 * @note 阶段：F-D victim↔Actor 可审计绑定。
 */
internal data class VictimIdentity(
    val pid: Int,
    val startTimeTicks: Long,
    val uid: Long,
)

/** uid 读回口的实现种类；生产路径必须显式声明 [PRODUCTION]。 */
internal enum class VictimProbeKind { FIXTURE, PRODUCTION }

/**
 * 受害者 uid 读回口：生产实现是 [ProcessVictimSession]，fixture 用于宿主验证。
 *
 * @note 阶段：G4L-6 victim；本接口不做 I/O，具体实现拥有进程生命周期。
 */
internal interface VictimProbe {
    /** @return 实现种类；fixture 默认值，生产实现必须覆盖。 */
    val kind: VictimProbeKind get() = VictimProbeKind.FIXTURE

    /**
     * 读取受害者当前 uid。
     *
     * @return [VictimUidResult.Ok] 或具名失败；实现必须有界、不得阻塞调用方。
     */
    fun uid(): VictimUidResult

    /**
     * 读取 victim 身份证据（PID + starttime + uid）。
     *
     * @return 身份证据；实现不支持/读取失败时返回 null（验收按缺失拒绝）。
     * @note 默认返回 null；生产实现 [ProcessVictimSession] 覆盖。
     */
    fun identity(): VictimIdentity? = null
}
