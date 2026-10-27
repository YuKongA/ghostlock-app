package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.core.flow.FlowRequest

/**
 * 一次 43499 run 的完整输入，形状对齐历史 `KernelMemoryRun`，字段权威是
 * `core/src/jni/attack_contract_fields.def`（HEAD 已删，可恢复于
 * `git show ecf3e82c:core/src/jni/attack_contract_fields.def`）：
 *
 * session(Init43499 45 + KernelLayout 37 + CredentialLayout 15 + TargetOffsets 20)
 * + prepare(39) + discover(30；native 31，prepareGeneration 由 Flow 代填)。
 * 步骤不在 request 里：Flow 严格控制固定步骤，可变步骤由 Lua 负责。
 *
 * 不进入 POD 的 Kotlin 侧策略：expectedUid（成功判据）、finishAction
 * （RETIRE/PARK；HANDOFF 已裁不做）；重试/间隔由 Flow 的 RetryPolicy/StepTiming
 * 独占，本类不另存 attempts/settleMs。
 *
 * @property session 四组 init DTO；实例由导入配置阶段保证正确，request 不再检查。
 * @property prepare 准备阶段参数。
 * @property discover 发现阶段参数；prepareGeneration 不允许调用方给。
 * @property expectedUid Kotlin 侧成功判据，不是 native 参数。
 * @property finishAction Kotlin 侧终态策略，不是 native 参数。
 * @property runSpec G4L-1..G4L-6 线性链的显式输入（G4L-5/6 的 owner/run/finish 类型）；
 *   null 表示历史请求形状（仍按 `cve43499-flow-not-wired` fail-closed 兼容）。
 *   非 null 时 Flow 在 IO 上下文调用 `Cve43499Run`，顺序与资源终结由 Run 独占，
 *   Flow 不复制物理顺序/重试/fallback。
 * @note 阶段：实验接线（用户指令「把 flow43499impl 写好」）；真机未验证，不接默认
 *   backend runner、不宣称 supported。session/prepare/discover/finishAction 保持原样，
 *   只为兼容既有测试/UI 构造。
 */
data class Flow43499Request(
    val session: Session,
    val prepare: Prepare = Prepare(),
    val discover: Discover = Discover(),
    val expectedUid: Long = 0L,
    val finishAction: Flow43499FinishAction = Flow43499FinishAction.RETIRE,
    val runSpec: Flow43499RunSpec? = null,
) : FlowRequest {
    /** 四组 init DTO 一起给。 */
    data class Session(
        val init: Init,
        val layout: Flow43499KernelLayout = Flow43499KernelLayout(),
        val credentials: Flow43499CredentialLayout = Flow43499CredentialLayout(),
        val targets: Flow43499TargetOffsets = Flow43499TargetOffsets(),
    )

    /** 会话级 Init43499；route 是唯一权威，步骤不进入 request。 */
    data class Init(
        val route: Flow43499Route,
        val cpu: Cpu = Cpu(),
        val stamp: Stamp = Stamp(),
        val limits: Limits = Limits(),
        val timing: Timing = Timing(),
        val consumer: Consumer = Consumer(),
        val perf: Perf = Perf(),
        val spray: Spray = Spray(),
        val routeSelected: Long = 0L,
        val fakeTask: Long = 0L,
        val fakeLock: Long = 0L,
    )

    /** def 的 mainCpu/consumerCpu；-1 明确表示不绑定。 */
    data class Cpu(
        val mainCpu: Int = -1,
        val consumerCpu: Int = -1,
    )

    /** def 的四个 stamp 字段。 */
    data class Stamp(
        val waiterOffset: Long = 0L,
        val taskOffset: Long = 0L,
        val lockOffset: Long = 0L,
        val bufferSize: Long = 0L,
    )

    /** def 的 maxAttempts/armSequence/armHold。 */
    data class Limits(
        val maxAttempts: Long = 0L,
        val armSequence: Long = 0L,
        val armHold: Long = 0L,
    )

    /** def 的六个 timing 字段。 */
    data class Timing(
        val parkMs: Long = 0L,
        val settleUs: Long = 0L,
        val postRequeueSettleUs: Long = 0L,
        val postAdjustSettleUs: Long = 0L,
        val consumerDelayUs: Long = 0L,
        val fallbackLockUs: Long = 0L,
    )

    /** def 的四个 consumer 字段。 */
    data class Consumer(
        val burstCalls: Long = 0L,
        val maxCalls: Long = 0L,
        val niceModulus: Int = 0,
        val policy: Int = 0,
    )

    /** def 的 perfSlots + 4×{type,config}。 */
    data class Perf(
        val slots: Long = 0L,
        val event0: PerfEvent = PerfEvent(),
        val event1: PerfEvent = PerfEvent(),
        val event2: PerfEvent = PerfEvent(),
        val event3: PerfEvent = PerfEvent(),
    )

    data class PerfEvent(
        val type: Long = 0L,
        val config: Long = 0L,
    )

    /** def 的 ring/spray/pump 五字段。 */
    data class Spray(
        val ringPages: Long = 0L,
        val ringWakeup: Long = 0L,
        val spraySize: Long = 0L,
        val sprayCount: Long = 0L,
        val pumpIters: Long = 0L,
    )

    /** native Prepare43499 的 39 个字段。 */
    data class Prepare(
        val collisions: Long = 0L,
        val threads: Long = 0L,
        val futexTableSize: Long = 0L,
        val roundWaitUs: Long = 0L,
        val wakeRounds: Long = 0L,
        val possibleCpus: Long = 0L,
        val mmSlabOrder: Long = 0L,
        val prepareMaxAttempts: Long = 0L,
        val prepareTimeoutMs: Long = 0L,
        val kernelSnitchTimeoutMs: Long = 0L,
        val routeWaitMs: Long = 0L,
        val routeDoneTimeoutMs: Long = 0L,
        val setupSettleUs: Long = 0L,
        val statePollIntervalUs: Long = 0L,
        val headerBytes: Long = 0L,
        val totalPokes: Long = 0L,
        val pageSize: Long = 0L,
        val regionSize: Long = 0L,
        val objectSize: Long = 0L,
        val mmStructSize: Long = 0L,
        val regionThreads: Long = 0L,
        val mainCpu: Int = -1,
        val pollRounds: Long = 0L,
        val preGroups: Long = 0L,
        val preShapedGroups: Long = 0L,
        val sprayGroups: Long = 0L,
        val postGroups: Long = 0L,
        val leakGroups: Long = 0L,
        val wordBytes: Long = 0L,
        val handleBytes: Long = 0L,
        val coarseStride: Long = 0L,
        val slabStride: Long = 0L,
        val mmObjectsPerSlab: Long = 0L,
        val shaped: Boolean = false,
        val socketBuffer: Long = 0L,
        val sendSize: Long = 0L,
        val proofSeed: Long = 0L,
        val proofCount: Long = 0L,
        val counterOffset: Long = 0L,
    )

    /**
     * native Discover43499 共 31 个字段；本类 30 个，prepareGeneration 由
     * Flow 代填。
     *
     * generation 必须来自上一次 prepare 的 facts；调用方不得直接提供。
     */
    data class Discover(
        val collisions: Long = 0L,
        val threads: Long = 0L,
        val futexTableSize: Long = 0L,
        val roundWaitUs: Long = 0L,
        val wakeRounds: Long = 0L,
        val possibleCpus: Long = 0L,
        val mmSlabOrder: Long = 0L,
        val headerBytes: Long = 0L,
        val totalPokes: Long = 0L,
        val pageSize: Long = 0L,
        val regionSize: Long = 0L,
        val objectSize: Long = 0L,
        val mmStructSize: Long = 0L,
        val regionThreads: Long = 0L,
        val mainCpu: Int = -1,
        val pollRounds: Long = 0L,
        val preGroups: Long = 0L,
        val preShapedGroups: Long = 0L,
        val sprayGroups: Long = 0L,
        val postGroups: Long = 0L,
        val leakGroups: Long = 0L,
        val wordBytes: Long = 0L,
        val handleBytes: Long = 0L,
        val rangeStart: Long = 0L,
        val rangeEnd: Long = 0L,
        val counterOffset: Long = 0L,
        val sweepTags: Long = 0L,
        val patienceRounds: Long = 0L,
        val coarseStride: Long = 0L,
        val slabStride: Long = 0L,
    )

}
