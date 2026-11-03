package com.ghostlock.app.backend.backends.cve43499.victim

/**
 * A1/A2a provisional（task-91）：43499 victim 终结/超时策略（backend 侧，不修改共享 core）。
 *
 * 默认 43499 语义：
 * - [killOnTimeout]=false：finish 超时**只 kill 脚本 child**，不 kill/destroy victim（A1 delta）；
 * - [killOnClose]=false：rooted victim 的 close 走 detach/park（不 kill/reap）；
 *   非 rooted 仍允许正常回收（`shouldCloseKill(rooted=false)` 恒 true，除非显式要求保留）；
 * - [detachOnTimeout]=true：超时路径优先 detach/park 而不是强杀。
 *
 * @note 公共 `VictimSessionConfig` 当前无 killPolicy 字段 ⇒ 本策略由 backend runtime/adapter
 *   携带；**D0 冻结后映射公共 spec**（届时字段上浮，本类作 43499 默认值来源）。
 * @note 阶段：provisional；按用户令未编译/未跑单测；真机 UNVERIFIED。
 */
internal data class Cve43499VictimKillPolicy(
    val killOnTimeout: Boolean = false,
    val killOnClose: Boolean = false,
    val detachOnTimeout: Boolean = true,
) {
    /**
     * @param rooted victim 是否已观察到 root。
     * @return true=close 允许 kill/回收；false=只 detach/park。
     * @note 非 rooted 仍正常回收（可回收），rooted 默认不杀（killOnClose=false）。
     */
    fun shouldCloseKill(rooted: Boolean): Boolean = if (rooted) killOnClose else true

    /**
     * @param rooted victim 是否已观察到 root。
     * @return true=超时只 kill 脚本 child（不 kill victim）。
     */
    fun shouldKeepVictimOnTimeout(rooted: Boolean): Boolean =
        !killOnTimeout || (rooted && !killOnTimeout)

    companion object {
        /** 43499 默认策略（A1 语义）。 */
        val DEFAULT: Cve43499VictimKillPolicy = Cve43499VictimKillPolicy()
    }
}
