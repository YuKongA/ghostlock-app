package com.ghostlock.app.core.flow

/**
 * Flow 运行期进度回调（B2）。
 *
 * 只承载**人类可读事实行**（service marker/阶段名），不参与控制流、不决定成功/失败、
 * 不改变 retry/timeout/cancel 语义（三者仍由 [Flow] 唯一决定，ADR-0022）。
 *
 * @note 默认实现 [NONE] 为 no-op；UI 注入时把行转发到运行页终端。
 */
fun interface FlowProgress {
    /**
     * @param line 单行事实（调用方保证 ≤512 B、不含控制字符/密钥）。
     * @return 无；实现不得抛异常（抛异常不得中断 Flow）。
     */
    fun onLine(line: String)

    companion object {
        /** 空实现：不需要进度的调用方使用。 */
        val NONE: FlowProgress = FlowProgress { }
    }
}
