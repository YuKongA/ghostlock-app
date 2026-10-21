package com.ghostlock.app.engine.ktstep

import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.OpsReply
import com.ghostlock.app.core.steps.StepId

/** 一步的可核证据：布局、页结论、路由结论、页副本。 */
data class KtStepTrace(
    val step: StepId,
    val layout: WriteLayout,
    val pageVerdict: LayoutVerdict,
    val route: RouteResult?,
    val page: ByteArray?,
)

/**
 * 把 PipelineEngine/StepPlans 的 write_word 映射成：Kotlin 布局 → 页 → 路由尝试。
 *
 * 不读 profile、不选 route、不做重试（重试/冻结由 PipelineEngine 决定）。
 */
class KtOpsInvoker(
    private val route: KernelRoute,
    private val plan: KtStepPlan,
) : OpsInvoker {
    val traces = mutableListOf<KtStepTrace>()
    private val byAddress: Map<Long, KtStep>

    init {
        val addresses = plan.steps.values.map { it.target }
        require(addresses.distinct().size == addresses.size) { "step targets must be unique" }
        byAddress = plan.steps.values.associateBy { it.target }
    }

    override fun invoke(
        opName: String,
        values: Map<String, ULong>,
        blobs: Map<String, ByteArray>,
    ): OpsReply {
        if (opName != "write_word") return refused("unsupported", "op-unknown:" + opName)
        val address = values["address"]?.toLong()
            ?: return refused("bad-parameters", "key-missing:address")
        val step = byAddress[address]
            ?: return refused("bad-parameters", "step-address-unknown:" + address)
        val request = WriteRequest(step.target, step.mode, step.preserveChild, step.value)
        val layout = writeLayout(request, plan.layoutInputs)
        if (!layoutAcceptsPage(request, layout)) {
            traces += KtStepTrace(step.step, layout, LayoutVerdict.LayoutInvalid, null, null)
            return refused("failed", "layout:" + step.step.token)
        }
        val page = ByteArray(plan.pageSize)
        val verdict = buildPayloadPage(page, plan.pageInputs, request, layout)
        if (verdict != LayoutVerdict.Ok) {
            traces += KtStepTrace(step.step, layout, verdict, null, null)
            return refused("failed", "page:" + verdict.name + ":" + step.step.token)
        }
        val result = route.attempt(request, page, plan.pageInputs)
        traces += KtStepTrace(step.step, layout, verdict, result, page.copyOf())
        return when (result) {
            is RouteResult.Landed -> OpsReply(
                status = "ok",
                refusal = null,
                values = mapOf("width" to 8uL, "attempts" to result.attempts.toULong()),
                blobs = emptyMap(),
            )
            is RouteResult.Refused -> OpsReply(
                status = if (result.retryable) "not-ready" else "failed",
                refusal = "route:" + result.reason,
                values = emptyMap(),
                blobs = emptyMap(),
            )
            is RouteResult.Indeterminate -> OpsReply(
                status = "indeterminate",
                refusal = "route:" + result.reason,
                values = emptyMap(),
                blobs = emptyMap(),
            )
        }
    }

    private fun refused(status: String, reason: String): OpsReply =
        OpsReply(status, reason, emptyMap(), emptyMap())
}
