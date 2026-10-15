package com.ghostlock.app.core.steps

/**
 * 步骤的输入：步骤要用的那些值，全部由调用方（J8 之前是临时界面，之后是 profile 装载层）给。
 *
 * 这里**没有默认值**是有意的：一个没给值的步骤应当拒绝（precondition），而不是拿一个猜出来的
 * 默认值去动内核。重试次数与间隔也来自这里 —— 引擎自己不发明耐心。
 */
class StepInputs(
    /** 每次调用允许的尝试次数；1 表示不重试。 */
    val attempts: Int = 1,
    /** 两次尝试之间的等待（毫秒）。 */
    val settleMs: Long = 0L,
    /** w1：SELinux enforcing 字段的地址（别名后）。 */
    val selinuxAddress: ULong? = null,
    /** w2：凭据字段的地址。 */
    val credentialAddress: ULong? = null,
    /** w2：要写入的凭据片段值。 */
    val credentialValue: ULong? = null,
    /** w3：seccomp 字段的地址。 */
    val seccompAddress: ULong? = null,
    /** write_word 的路由内偏移。 */
    val writeOffset: ULong = 0uL,
    /** 43284：三个角色的路径。 */
    val patch1Path: ByteArray? = null,
    val carrierPath: ByteArray? = null,
    val hookPath: ByteArray? = null,
    /** carrier：载体里的起始偏移。 */
    val carrierBase: ULong = 0uL,
    /** trigger：延迟（毫秒）。 */
    val triggerDelayMs: ULong? = null,
    /** wait：预算与轮询间隔（毫秒），以及模块名。 */
    val waitBudgetMs: ULong? = null,
    val waitPollMs: ULong? = null,
    val moduleName: ByteArray? = null,
) {
    init {
        require(attempts >= 1) { "attempts must be at least one call" }
        require(settleMs >= 0L) { "settle must not be negative" }
    }
}
