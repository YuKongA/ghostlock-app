package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishVerdict
import com.ghostlock.app.backend.cve43499.run.Cve43499Run
import com.ghostlock.app.backend.cve43499.run.Cve43499RunConfig
import com.ghostlock.app.backend.cve43499.run.Cve43499RunOutcome
import com.ghostlock.app.backend.cve43499.run.Cve43499RunReceipts
import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepResult
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * 43499 的 Flow 实现：把 G4L-1..G4L-6 的线性链**业务组合**起来（用户指令
 * 「把 flow43499impl 写好」，授权记录见
 * `docs/analysis/43499-flow43499impl-20261026.md`）。
 *
 * 结构约定：
 * - 本类只组装**一个** step：把 [Flow43499Request.runSpec] 交给 `Cve43499Run`；
 * - 物理顺序（victim → placement → landing → actor → execute → uid probe →
 *   finish → 验收 → close）与资源终结**由 `Cve43499Run` 独占**，本类不复制顺序、
 *   不做重试、不做 fallback，cleanup 也不重复关资源（Run 的 finally 已收）；
 * - 执行放在 `Dispatchers.IO`：Run 内部是阻塞 syscall/进程等待，不得占用调用线程；
 * - 不新增可写全局/单例，不接默认 backend runner 或 UI 入口。
 *
 * 失败语义（全部具名，不伪成功）：
 * - `runSpec == null`：历史请求形状（既有测试/调用方只填 session/prepare/discover），
 *   保持 fail-closed 的 `cve43499-flow-not-wired`，与既有测试兼容；
 * - `runSpec` 存在但必填项缺失（当前为空白 runId）：`cve43499-run-request-missing`；
 * - Run 返回 Refused：reasons 逗号连接作为 step reason，evidence 带完整 receipt；
 * - Run 抛异常：`cve43499-run-exception:<Type>`（`CancellationException` 继续上抛）。
 *
 * @note 阶段：**实验实现**。未过真机门禁/评审，不得据此宣称 supported、uid=0 或
 *   生产可用；不要把它设为默认 backend 入口。
 */
class Flow43499Impl : SequentialFlow<Flow43499Request>(Flow43499Impl::class) {
    override fun steps(request: Flow43499Request): List<FlowStep> {
        val runSpec = request.runSpec
        if (runSpec == null) {
            /* 历史请求形状：保持既有 fail-closed 契约（兼容既有 flow 测试）。 */
            return listOf(
                FlowStep("cve43499.not-wired") {
                    StepResult.Failed(REASON_LEGACY_NOT_WIRED)
                },
            )
        }
        if (runSpec.runId.isBlank()) {
            return listOf(
                FlowStep("cve43499.run-request") {
                    StepResult.Failed(
                        REASON_RUN_REQUEST_MISSING,
                        mapOf(EVIDENCE_RUN_ID to runSpec.runId),
                    )
                },
            )
        }
        return listOf(
            FlowStep("cve43499.run") {
                runChain(runSpec)
            },
        )
    }

    /**
     * 在 IO 上下文执行一次完整线性链，并把结局映射成 step 结果。
     *
     * @param runSpec 已通过必填校验的线性链输入。
     * @return Completed ⇒ Done（evidence 含 runId/verdict/receipts/uid/termination）；
     *   Refused ⇒ Failed（reasons 连接 + 同 evidence）。
     * @note 异常标识保留：`CancellationException` 继续上抛；其它映射为具名 Failed。
     */
    private suspend fun runChain(runSpec: Flow43499RunSpec): StepResult =
        withContext(Dispatchers.IO) {
            val runId = runSpec.runId
            val config = Cve43499RunConfig(
                placement = runSpec.placement,
                landing = runSpec.landing,
                actor = runSpec.actor,
                handoff = runSpec.handoff,
                runId = runId,
            )
            val runner = Cve43499Run(
                config = config,
                finishExecutor = { handoff, id ->
                    runSpec.finishExecutor?.invoke(handoff, id)
                        ?: emptyExecutionEvidence(id)
                },
            )
            try {
                when (val outcome = runner.run()) {
                    is Cve43499RunOutcome.Completed ->
                        StepResult.Done(evidence(outcome.receipts))
                    is Cve43499RunOutcome.Refused ->
                        StepResult.Failed(
                            outcome.reasons.joinToString(","),
                            evidence(outcome.receipts),
                        )
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                StepResult.Failed(
                    REASON_RUN_EXCEPTION_PREFIX + problem::class.java.simpleName,
                    mapOf(EVIDENCE_RUN_ID to runId),
                )
            }
        }

    /**
     * 把 Run 的物理回执映射成 step evidence（全部字符串；不改判定语义）。
     *
     * @param receipts Run 的物理事实与收尾验收结论。
     * @return 稳定 key 的 evidence：runId/verdict/reasons/uid/termination/计数。
     */
    private fun evidence(receipts: Cve43499RunReceipts): Map<String, String> {
        val evidence = linkedMapOf<String, String>()
        evidence[EVIDENCE_RUN_ID] = receipts.runId
        evidence[EVIDENCE_VERDICT] = when (val verdict = receipts.verdict) {
            FinishVerdict.Accepted -> VERDICT_ACCEPTED
            is FinishVerdict.Refused -> {
                evidence[EVIDENCE_REASONS] = verdict.reasons.joinToString(",")
                VERDICT_REFUSED
            }
        }
        evidence[EVIDENCE_UID] = receipts.victimUid?.toString() ?: VALUE_MISSING
        evidence[EVIDENCE_TERMINATION] = receipts.termination
        evidence[EVIDENCE_PLACEMENT_ROUNDS] = receipts.placementRounds.toString()
        evidence[EVIDENCE_LANDING_RECLAIM_SENDS] =
            receipts.landingReclaimSends.toString()
        evidence[EVIDENCE_ACTOR_CONSUMER_SUCCESS] =
            receipts.actorConsumerSuccess.toString()
        receipts.victimStartTimeTicks?.let {
            evidence[EVIDENCE_VICTIM_STARTTIME] = it.toString()
        }
        receipts.executionExitCode?.let {
            evidence[EVIDENCE_EXECUTION_EXIT] = it.toString()
        }
        return evidence
    }

    /**
     * 构造「未提供执行 seam」时的空执行证据。
     *
     * @param runId 本次运行 id。
     * @return exit/marker/摘要均为空的证据；验收会以 `finish-execution-missing`
     *   具名拒绝，因此不会伪成功。
     */
    private fun emptyExecutionEvidence(runId: String): FinishExecutionEvidence =
        FinishExecutionEvidence(
            runId = runId,
            exitCode = null,
            timedOut = false,
            outputDigestSha256 = null,
            successMarkerContent = null,
            failureMarkerContent = null,
        )

    private companion object {
        /** 历史请求形状的 fail-closed reason（与既有测试冻结的字符串一致）。 */
        const val REASON_LEGACY_NOT_WIRED: String = "cve43499-flow-not-wired"

        /** run 输入缺失的具名 reason。 */
        const val REASON_RUN_REQUEST_MISSING: String = "cve43499-run-request-missing"

        /** Run 抛异常时的 reason 前缀。 */
        const val REASON_RUN_EXCEPTION_PREFIX: String = "cve43499-run-exception:"

        const val EVIDENCE_RUN_ID: String = "runId"
        const val EVIDENCE_VERDICT: String = "verdict"
        const val EVIDENCE_REASONS: String = "reasons"
        const val EVIDENCE_UID: String = "uid"
        const val EVIDENCE_TERMINATION: String = "termination"
        const val EVIDENCE_PLACEMENT_ROUNDS: String = "placementRounds"
        const val EVIDENCE_LANDING_RECLAIM_SENDS: String = "landingReclaimSends"
        const val EVIDENCE_ACTOR_CONSUMER_SUCCESS: String = "actorConsumerSuccess"
        const val EVIDENCE_VICTIM_STARTTIME: String = "victimStartTimeTicks"
        const val EVIDENCE_EXECUTION_EXIT: String = "executionExitCode"
        const val VERDICT_ACCEPTED: String = "accepted"
        const val VERDICT_REFUSED: String = "refused"
        const val VALUE_MISSING: String = "missing"
    }
}
