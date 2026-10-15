package com.ghostlock.app.core.steps

/**
 * 步骤标识：名字就是 profile/wire 里的拼写（native 的 step_catalog 用同一批 token）。
 *
 * 两个 backend 的步骤集：
 *  - 43499：w1（SELinux 解除）、w2（凭据伪造）、w3（seccomp 清理）
 *  - 43284：patch1（helper 落页缓存）、carrier（模块镜像落载体）、hook（sentry 钩子）、
 *           trigger（双 fork 触发）、wait（终态等待）
 */
enum class StepId(val token: String) {
    W1("w1"),
    W2("w2"),
    W3("w3"),
    Patch1("patch1"),
    Carrier("carrier"),
    Hook("hook"),
    Trigger("trigger"),
    Wait("wait"),
    ;

    companion object {
        /** @return 该 token 对应的步骤，未知则 null（调用方按具名拒绝处理，不静默跳过）。 */
        fun ofToken(token: String): StepId? = entries.firstOrNull { it.token == token }
    }
}

/** 一步的结局。Frozen 是引擎自己的状态：出现不确定效果之后不再前进。 */
enum class StepOutcome { Ran, Refused, Frozen }

/**
 * 一步的结果。
 *
 * @property reason 具名理由（拒绝时是 native 的 refusal 或引擎自己的 precondition:xxx）。
 * @property calls  这一步实际发出的原生调用名，按顺序 —— 让"它到过哪"可核。
 */
data class StepResult(
    val step: StepId,
    val outcome: StepOutcome,
    val reason: String? = null,
    val calls: List<String> = emptyList(),
)
