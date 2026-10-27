package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.cve43499.flow.Flow43499Impl
import com.ghostlock.app.backend.cve43499.flow.Flow43499Request
import com.ghostlock.app.backend.cve43499.flow.Flow43499RunSpec
import com.ghostlock.app.backend.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.core.flow.FlowResult
import java.util.UUID
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * 43499 实验运行链在 UI 层的可观察状态。
 *
 * @note 状态只属于页面实例（`remember` 在 Composition 内创建），不是全局单例。
 * @note 阶段：task-19「接到 UI」；实验态、未过真机门禁，不得作为 supported 宣称。
 */
sealed interface Cve43499RunUiState {
    /** 尚未运行（或页面刚创建）。 */
    data object Idle : Cve43499RunUiState

    /** 正在执行：按钮应禁用，UI 显示运行中。 */
    data object Running : Cve43499RunUiState

    /** Flow 返回了结果（Ok/Failed）；UI 只原样展示，不做二次判定。 */
    data class Finished(val result: FlowResult) : Cve43499RunUiState

    /** 执行前/执行中被具名拒绝；reasons 稳定可检索。 */
    data class Refused(val reasons: List<String>) : Cve43499RunUiState
}

/**
 * 本次 UI 能提供的物理输入。
 *
 * @property placement Placement 定位事务参数。
 * @property landing landing 参数（含最终页面字节；当前 Kotlin 侧**没有**可信计算来源）。
 * @property actor Actor 调度与 Carrier（含最终地址/frame；同样没有可信来源）。
 * @note 默认全 null：本批要求「缺输入不得伪造」。是否允许运行由 UI 框架
 *   `BackendRunGate` 决定；若门禁允许而执行期仍缺输入，controller 返回具名
 *   `cve43499-run-execution:inputs-incomplete:<fields>`（执行期失败，不是 allow/deny 决策）。
 * @note payload/地址计算批完成后由该批提供真实值，这里无需改状态机。
 */
internal data class Cve43499PhysicalInputs(
    val placement: PlacementConfig? = null,
    val landing: LandingConfig? = null,
    val actor: ActorPreparation? = null,
)

/**
 * CVE-2026-43499 实验链的 UI 控制器（Compose 状态 + 显式依赖注入）。
 *
 * 职责边界（task-21 架构要求）：
 * - **是否允许运行**由 UI 框架 `BackendRunGate` 唯一决定；本类不再做 root/环境 allow/deny 前置判断；
 * - 本类只执行：从 [FinishConfig] 当前 `FinishState` 经 [FinishHandoffAdapter] 构建
 *   [FinishHandoff] → 组装 [Flow43499RunSpec]（finishExecutor 用 UI 侧 [FinishExecution]）
 *   → 在 `Dispatchers.IO` 调用 [Flow43499Impl].run(request)；
 * - 执行期失败仍以具名结果返回、不吞掉：
 *   `cve43499-run-execution:finish-handoff:<reason>`、
 *   `cve43499-run-execution:inputs-incomplete:<fields>`、
 *   `cve43499-run-execution:exception:<Type>`。
 *
 * @param finishState 当前收尾配置 seam（默认 [FinishConfig.state]）。
 * @param physicalInputs 物理输入 seam（默认全 null；由后续 payload/地址计算批提供真实值）。
 * @param legacyRequestFactory legacy `Flow43499Request`（session/prepare/discover）组装 seam；
 *   这些 DTO 没有可信 UI 来源，故默认返回 null（执行期具名失败）；后续 profile 批提供。
 * @note 阶段：实验（task-19/task-21）；真机未验证，不得默认启用/宣称 supported。
 */
internal class Cve43499RunController(
    private val finishState: () -> FinishState = { FinishConfig.state },
    private val physicalInputs: () -> Cve43499PhysicalInputs = { Cve43499PhysicalInputs() },
    private val legacyRequestFactory: (Flow43499RunSpec) -> Flow43499Request? = { null },
) {
    /** 当前 UI 状态；只在主线程被写（[run] 在 Composition 协程里调用）。 */
    var state: Cve43499RunUiState by mutableStateOf(Cve43499RunUiState.Idle)
        private set

    /**
     * 执行一次实验链；重复点击在 Running 状态下直接忽略。
     *
     * @param context 应用上下文（读收尾配置/写脚本对象用）。
     * @note 本函数不抛异常：全部失败映射为 [Cve43499RunUiState.Refused]；
     *   `CancellationException` 继续上抛（页面销毁时协程取消）。
     */
    suspend fun run(context: Context) {
        if (state is Cve43499RunUiState.Running) return
        state = Cve43499RunUiState.Running
        state = execute(context)
    }

    /**
     * 执行主体：handoff → 输入组装 → 调 Flow → 映射状态。
     *
     * @param context 应用上下文。
     * @return Finished 或 Refused；不会返回 Running。
     * @note allow/deny 已由 `BackendRunGate` 决定；本函数只产出执行期具名结果。
     */
    private suspend fun execute(context: Context): Cve43499RunUiState {
        val runId = RUN_ID_PREFIX + UUID.randomUUID().toString()
        val handoff = runCatching {
            FinishHandoffAdapter.build(context = context, state = finishState())
        }.getOrNull() ?: return Cve43499RunUiState.Refused(
            listOf(REASON_HANDOFF_PREFIX + REASON_HANDOFF_BUILD_FAILED),
        )

        val inputs = runCatching { physicalInputs() }
            .getOrElse { Cve43499PhysicalInputs() }
        val missing = missingPhysicalFields(inputs)
        if (missing.isNotEmpty()) {
            return Cve43499RunUiState.Refused(
                listOf(REASON_INPUTS_INCOMPLETE_PREFIX + missing.joinToString(",")),
            )
        }
        val spec = buildRunSpec(inputs, handoff, runId)
        val request = runCatching { legacyRequestFactory(spec) }
            .getOrNull() ?: return Cve43499RunUiState.Refused(
            listOf(
                REASON_INPUTS_INCOMPLETE_PREFIX + LEGACY_INPUT_FIELDS,
            ),
        )
        return try {
            val result = withContext(Dispatchers.IO) {
                Flow43499Impl().run(request)
            }
            Cve43499RunUiState.Finished(result)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Throwable) {
            Cve43499RunUiState.Refused(
                listOf(REASON_RUN_EXCEPTION_PREFIX + problem::class.java.simpleName),
            )
        }
    }

    /**
     * 物理输入缺失字段清单（具名、稳定顺序）。
     *
     * @param inputs 当前可用的物理输入。
     * @return 缺失字段名列表；空表示齐备。
     */
    private fun missingPhysicalFields(inputs: Cve43499PhysicalInputs): List<String> =
        buildList {
            if (inputs.placement == null) add("placement")
            if (inputs.landing == null) add("landing.finalBytes")
            if (inputs.actor == null) {
                add("actor.primitive.finalWaiterAddress")
                add("actor.primitive.finalTaskAddress")
                add("actor.primitive.finalLockAddress")
                add("actor.primitive.frameBytes")
            }
        }

    /**
     * 组装线性链输入 [Flow43499RunSpec]；finishExecutor 使用 UI 侧有界执行链。
     *
     * @param inputs 已确认齐备的物理输入。
     * @param handoff 已构建的收尾脚本冻结对象。
     * @param runId 本次运行 id。
     * @return 交给 [Flow43499Impl] 的线性链输入。
     * @note 前提：调用方已确保三项物理输入非 null（见 [missingPhysicalFields]）。
     */
    private fun buildRunSpec(
        inputs: Cve43499PhysicalInputs,
        handoff: FinishHandoff,
        runId: String,
    ): Flow43499RunSpec = Flow43499RunSpec(
        placement = requireNotNull(inputs.placement),
        landing = requireNotNull(inputs.landing),
        actor = requireNotNull(inputs.actor),
        handoff = handoff,
        runId = runId,
        finishExecutor = { script, id -> FinishExecution().execute(script, id) },
    )

    companion object {
        /** 运行 id 前缀；脚本 marker 内容会写入完整 runId。 */
        internal const val RUN_ID_PREFIX: String = "cve43499-ui-"

        /** 收尾 handoff 构建失败（执行期）的 reason 前缀。 */
        internal const val REASON_HANDOFF_PREFIX: String =
            "cve43499-run-execution:finish-handoff:"

        /** handoff 构建失败的第二段文本。 */
        internal const val REASON_HANDOFF_BUILD_FAILED: String = "build-failed"

        /** 物理/legacy 输入在执行期仍缺失的 reason 前缀。 */
        internal const val REASON_INPUTS_INCOMPLETE_PREFIX: String =
            "cve43499-run-execution:inputs-incomplete:"

        /** legacy transport（session/prepare/discover）缺失字段名。 */
        internal const val LEGACY_INPUT_FIELDS: String = "session,prepare,discover"

        /** 运行异常（执行期）的 reason 前缀。 */
        internal const val REASON_RUN_EXCEPTION_PREFIX: String =
            "cve43499-run-execution:exception:"
    }
}
