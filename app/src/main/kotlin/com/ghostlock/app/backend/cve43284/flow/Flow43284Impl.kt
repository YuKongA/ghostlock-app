package com.ghostlock.app.backend.cve43284.flow

import com.ghostlock.app.core.flow.FlowRequest
import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepResult

/**
 * 43284 的完整 Flow 实现。
 *
 * 本类自己组装步骤与 cleanup；UI 只传 FlowRequest，不注入 steps。
 * 攻击逻辑尚未接线时，返回具名失败，而不是伪造成功。
 */
class Flow43284Impl : SequentialFlow(Flow43284Impl::class) {
    override fun steps(request: FlowRequest): List<FlowStep> {
        return listOf(
            FlowStep("cve43284.not-wired") {
                StepResult.Failed("cve43284-flow-not-wired")
            },
        )
    }
}
