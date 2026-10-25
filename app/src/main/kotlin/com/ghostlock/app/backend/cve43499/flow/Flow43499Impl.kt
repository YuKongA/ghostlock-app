package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepResult

/**
 * 43499 的完整 Flow 实现。
 *
 * 本类自己组装固定步骤与 cleanup；UI 只传 [Flow43499Request]，request 不含步骤。
 * 本类只经 Cve43499BackendPort 调 Native43499，不直接触 JNI。
 * 可变步骤由 Lua 负责；Flow 的固定链不得从 request 注入或改写。
 * 攻击逻辑尚未接线时，返回具名失败，而不是伪造成功。
 */
class Flow43499Impl : SequentialFlow<Flow43499Request>(Flow43499Impl::class) {
    override fun steps(request: Flow43499Request): List<FlowStep> {
        return listOf(
            FlowStep("cve43499.not-wired") {
                StepResult.Failed("cve43499-flow-not-wired")
            },
        )
    }
}
