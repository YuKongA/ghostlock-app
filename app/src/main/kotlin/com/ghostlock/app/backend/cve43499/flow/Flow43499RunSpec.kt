package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteGenerator
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.backend.cve43499.owned.KernelsuGuardResult
import com.ghostlock.app.backend.cve43499.owned.ProcessLimitsOutcome
import com.ghostlock.app.backend.cve43499.victim.VictimSession
import com.ghostlock.app.backend.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499PrimitiveSpec

/**
 * post-placement 生成产物：landing 输入 + Actor 绑定的原生 Carrier。
 *
 * @property landing 由候选页基址派生的最终页面字节与 landing 几何。
 * @property primitive 由候选页基址/测量地址派生的 Carrier 地址、frame 与 route 参数。
 * @note 阶段：R3a-1；只在 Placement 完成（拿到 facts）后由 generator 产出。
 */
internal data class Cve43499GeneratedInputs(
    val landing: LandingConfig,
    val primitive: Native43499PrimitiveSpec,
)

/**
 * post-placement generator seam：`(placement facts) -> landing + primitive`。
 *
 * 依赖方向（R3 审计共性 1 / B1）：payload/stamp/frame/地址必须由 placement 实测的
 * `candidatePageBase` 派生；在 placement 之前冻结属于依赖倒置。Flow 在
 * `cve43499.placement.prepare` 之后、`cve43499.page.land` 之前调用本 seam。
 *
 * @note 实现必须是纯函数（同一 facts 产出同一输入），不得持有 token、不得触 Native。
 * @note 阶段：R3a-1；三路 generator（multicast/pselect/tcp）由 R3b 实现并注入。
 */
internal fun interface Cve43499PostPlacementGenerator {
    /**
     * @param facts Placement 事务回传的只读物理事实。
     * @return 本次运行的 landing 输入与 Carrier 描述。
     */
    fun generate(facts: Cve43499PlacementFacts): Cve43499GeneratedInputs
}

/**
 * 43499 线性链（G4L-1..G4L-6）的显式输入持有者。
 *
 * 权威：`docs/analysis/43499-flow43499impl-20261026.md`（用户「把 flow43499impl 写好」
 * 的授权记录）+ `docs/analysis/43499-three-route-line-review-20261027.md`（R3 数据流）。
 * 本类只承载已冻结的 Physical/Finish 输入，不含步骤表、不含 route、不含重试/fallback
 * 策略 —— 顺序由 `Flow43499Impl` 独占、资源终结由 `Cve43499Lifecycle`（唯一生命周期层）
 * 独占。
 *
 * 数据流（R3a-1）：
 * ```
 * placement.prepare → Cve43499PlacementFacts
 *   → postPlacementGenerator(facts) → Cve43499GeneratedInputs(landing, primitive)
 *   → page.land(landing) → actor.prepare(ActorPreparation(actorSetup, primitive))
 * ```
 * [frozenInputs] 是历史 P0b 接线路径的冻结兼容输入：仅当 [postPlacementGenerator]
 * 为 null 时使用；generator 非 null 时一律以 facts 生成值为准。
 *
 * @property placement Placement 定位事务参数（CPU/超时/扫描窗口/对齐）。
 * @property actorSetup 三线程 Actor 的静态调度参数（与 placement 事实无关）。
 * @property frozenInputs 冻结兼容输入（landing + primitive）；null 表示必须由
 *   [postPlacementGenerator] 生成。
 * @property handoff UI 生成的收尾脚本冻结对象；后端只读、不生成/不解释脚本。
 * @property runId 本次运行 id；marker 内容、执行证据与 victim 绑定都以它为键。
 * @property postPlacementGenerator placement 完成后的生成 seam（R3 三路数据平面）；
 *   null ⇒ 走 [frozenInputs] 兼容路径。
 * @property finishExecutor **已废弃（A1 finish victim-only）**：finish 唯一执行点=
 *   rooted victim（`VictimFinish`）；本字段保留仅作源码兼容（本批不删），**不再被执行**。
 *   null/非 null 都不产生 App/su 执行；无 rooted victim ⇒ 具名
 *   `cve43499:finish.execute:su-fallback-refused`。
 * @property writeChain R3d 多写链输入（target source/policy/verifier/restarter）；
 *   null ⇒ Flow 保持 R3b 单 W1 行为（历史兼容路径）。
 * @property writeGenerator R3d 按写请求生成 landing/primitive 的 seam；多写链启用时必填
 *   （三路 generator 同时实现 post-placement 与按请求生成两个 seam）。
 * @property finishTimeoutMs R3f rooted victim 内 finish 脚本的有界等待（毫秒；必须 >0）。
 * @property ksuCheck G07 可选 KSU 加载守卫（W1 前执行；Loaded ⇒ 具名拒绝并留 evidence）。
 * @property processLimitsRaiser G06 可选 RLIMIT 抬升 seam（W1 前执行；失败 ⇒ 具名拒绝）。
 * @property failStopEnabled G01 dirty/retained fail-stop 开关（debug `failStop.enabled`）。
 * @property failStopTerminator 可选终止口；null ⇒ 生产默认 `Process.killProcess(myPid())`，
 *   宿主单测必须注入 fake（不得真杀）。
 * @property failStopEvidenceSink 可选 evidence 落盘/flush 口（在终止前调用）。
 * @property elapsedRealtimeNanos G05 计时 seam（默认 `SystemClock.elapsedRealtimeNanos()`；
 *   宿主单测注入 fake，避免 JVM 触 Android）。
 * @property victimFactory R4/批 B2 可选 victim 会话工厂；null ⇒ lifecycle 默认
 *   `ProcessVictimSession`（单 W1/默认路径行为不变）。非 null 时 Flow 用它构造
 *   `Cve43499Lifecycle`（生产由 RequestFactory 在 `injection.victim=jni-spawn` 时注入
 *   通用 `core/victim` helper 适配器；单测可注入 fake）。
 * @note 构造可见性为 internal：这是模块内部接线对象，不是公开 ABI，也不接默认 UI 入口。
 * @note 阶段：实验实现（flow 接线）；真机未验证，不得据此标 supported。
 */
class Flow43499RunSpec internal constructor(
    internal val placement: PlacementConfig,
    internal val actorSetup: Native43499ActorSetup,
    internal val frozenInputs: Cve43499GeneratedInputs? = null,
    internal val handoff: FinishHandoff,
    internal val runId: String,
    internal val postPlacementGenerator: Cve43499PostPlacementGenerator? = null,
    @Deprecated(
        message = "A1 finish victim-only: finish 唯一执行点=rooted victim；本 seam 不再被执行",
        level = DeprecationLevel.WARNING,
    )
    internal val finishExecutor: ((FinishHandoff, String) -> FinishExecutionEvidence)? = null,
    internal val writeChain: Cve43499WriteChain? = null,
    internal val writeGenerator: Cve43499WriteGenerator? = null,
    internal val finishTimeoutMs: Long = DEFAULT_FINISH_TIMEOUT_MS,
    internal val victimFactory: (() -> VictimSession)? = null,
    internal val ksuCheck: (() -> KernelsuGuardResult)? = null,
    internal val processLimitsRaiser: (() -> ProcessLimitsOutcome)? = null,
    internal val failStopEnabled: Boolean = false,
    internal val failStopTerminator: ((String) -> Unit)? = null,
    internal val failStopEvidenceSink: ((Map<String, String>) -> Unit)? = null,
    internal val elapsedRealtimeNanos: () -> Long = {
        android.os.SystemClock.elapsedRealtimeNanos()
    },
    /* task-86：victim 传输选择（process|jni-spawn|common；默认 PROCESS=现状）。 */
    internal val victimTransport: com.ghostlock.app.data.cve43499.DebugVictimTransport =
        com.ghostlock.app.data.cve43499.DebugVictimTransport.PROCESS,

    /* task-84/B3：acceptance 期望 uid（debug-only 可配；默认 root=0；负值 fail-closed）。 */
    internal val expectedUid: Long = 0L,

    /* B6：finish 派发前有界 settle + module/enforce 有界轮询（默认 2s；可注入）。 */
    internal val finishPreDispatchSettleMs: Long = 2_000L,
    internal val finishSleeper: (Long) -> Unit = { millis -> Thread.sleep(millis) },
    internal val finishEnforceReader: () -> Int? = {
        runCatching {
            java.io.File("/sys/fs/selinux/enforce").readText().trim().toIntOrNull()
        }.getOrNull()
    },
    internal val finishModuleStateReader: () -> String = {
        runCatching {
            val modules = java.io.File("/proc/modules").readText()
            when {
                modules.contains("kernelsu") || modules.contains("ksu") -> "loaded"
                else -> "absent"
            }
        }.getOrNull() ?: "unknown"
    },
) {
    companion object {
        /** rooted victim 内 finish 脚本的默认有界等待（毫秒；与既有执行层默认一致）。 */
        internal const val DEFAULT_FINISH_TIMEOUT_MS: Long = 30_000L
    }
}
