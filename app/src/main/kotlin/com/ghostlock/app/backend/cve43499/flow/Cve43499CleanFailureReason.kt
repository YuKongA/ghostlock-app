package com.ghostlock.app.backend.cve43499

/**
 * task-97：E1 有界重试的 clean-failure 判定（纯函数，生产/测试/ harness 共用）。
 *
 * 仅以下**可重试 clean failure** 返回 true（token 已恢复/未写/无 dirty）：
 * - landing page-accept：`landing:clean-failure` / `page.land:*clean-failure`；
 * - actor execute：`actor-execute:clean-failure`；
 * - actor prepare：`prepare-actor:clean-failure`。
 * 其它（dirty/receipt-incomplete/参数错误/非 clean RouteMiss）一律 false（立即 FAIL）。
 *
 * @param reason 事务 RouteMiss reason。
 * @return true=可安全重试。
 * @note 不解析 errno/字段，仅做稳定子串判定；detail 由 Lifecycle 追加在 clean-failure 之后。
 */
internal fun isRetryableCleanFailure(reason: String): Boolean {
    if (reason.contains("actor-execute:clean-failure")) return true
    if (reason.contains("prepare-actor:clean-failure")) return true
    if (reason.contains("landing:clean-failure")) return true
    if (reason.contains("page.land:") && reason.contains("clean-failure")) return true
    return false
}
