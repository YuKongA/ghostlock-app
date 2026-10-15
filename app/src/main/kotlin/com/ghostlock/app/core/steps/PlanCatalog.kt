package com.ghostlock.app.core.steps

/**
 * 声明的步骤集：谁跑哪些步、按什么顺序。
 *
 * 今天这张表是**代码里的常量**，因为 profile 装载（J8）还没接线；接线之后这张表由 wire 里的
 * steps 列表给出，本对象只保留"校验 + 按名取步"的职责（未知步名一律具名拒绝，不静默跳过）。
 */
object PlanCatalog {
    /** 两个 backend 的声明顺序。 */
    private val plans: Map<String, List<StepId>> = mapOf(
        "cve_2026_43499" to listOf(StepId.W1, StepId.W2, StepId.W3),
        "cve_2026_43284" to listOf(
            StepId.Patch1,
            StepId.Carrier,
            StepId.Hook,
            StepId.Trigger,
            StepId.Wait,
        ),
    )

    /** @return 该 backend 的步骤（顺序即执行顺序）；未知 backend 得到空表。 */
    fun declaredSteps(backendId: String): List<StepId> = plans[backendId].orEmpty()

    /**
     * 校验一份外部给的步骤名单（J8 之后来自 wire）。
     *
     * @return (步骤表, 拒绝名)；名单里出现未知 token 时返回 (空表, 具名拒绝)。
     */
    fun fromTokens(tokens: List<String>): Pair<List<StepId>, String?> {
        val steps = mutableListOf<StepId>()
        for (token in tokens) {
            val step = StepId.ofToken(token) ?: return emptyList<StepId>() to ("step-unknown:" + token)
            steps += step
        }
        return steps to null
    }
}
