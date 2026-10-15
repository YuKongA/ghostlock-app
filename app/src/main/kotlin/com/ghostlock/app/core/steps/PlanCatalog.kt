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
    /**
     * profile 里的步名到本表的步骤。
     *
     * 43284 的 profile 只写一个 pagecache_write —— 那是整条链的驱动名，本表展开成它的五个子步
     * （顺序即声明顺序）。这一层映射是有意的、只写一处：profile 说的是这台机器上跑哪条链，
     * 本表说的是这条链由哪几步构成。
     */
    private val profileAliases: Map<String, List<StepId>> = mapOf(
        "pagecache_write" to listOf(
            StepId.Patch1,
            StepId.Carrier,
            StepId.Hook,
            StepId.Trigger,
            StepId.Wait,
        ),
    )

    fun fromTokens(tokens: List<String>): Pair<List<StepId>, String?> {
        val steps = mutableListOf<StepId>()
        for (token in tokens) {
            profileAliases[token]?.let { expanded ->
                steps += expanded
                continue
            }
            val step = StepId.ofToken(token) ?: return emptyList<StepId>() to ("step-unknown:" + token)
            steps += step
        }
        return steps to null
    }
}
