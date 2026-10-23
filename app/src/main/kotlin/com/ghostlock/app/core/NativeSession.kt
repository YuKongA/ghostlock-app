package com.ghostlock.app.core

/**
 * 所有 native 会话的最小生命周期端口。
 *
 * 这里只统一“谁拥有会话、close 是什么”：不定义状态转移、不定义 cleanup 策略。
 * 每个 backend 的 session 自己实现初始化/关闭顺序与能力准入。
 */
interface NativeSession : AutoCloseable {
    val phase: SessionPhase

    /**
     * 非 suspend 的取消原语：只置 CLOSING / 取消活动任务。
     *
     * 不等待、不释放 native 句柄；native 释放由具体 session 的 shutdown() 完成。
     * 实现必须幂等。
     */
    override fun close()
}
