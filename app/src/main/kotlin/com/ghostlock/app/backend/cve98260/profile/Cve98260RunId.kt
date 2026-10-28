package com.ghostlock.app.backend.cve98260.profile

/**
 * 98260 run id 的唯一校验规则（参数化/通用化：避免 app/service/finish 三处漂移）。
 *
 * @note C 侧 payload 的 `glk_valid_run_id`（`timer.c`）是同一规则的跨语言镜像；
 *   修改本规则必须同批改 C 并重跑 B1 门禁（跨端一致性）。
 */
internal object Cve98260RunId {
    /** 合法字符与长度：8–64 位 `[A-Za-z0-9._-]`。 */
    internal val PATTERN: Regex = Regex("[A-Za-z0-9._-]{8,64}")

    /**
     * @param value 候选 run id。
     * @return true=合法。
     */
    internal fun valid(value: String): Boolean = PATTERN.matches(value)
}
