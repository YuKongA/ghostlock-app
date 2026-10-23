package com.ghostlock.app.core.access

/** 能力调用的统一结果；失败必须带具名 reason。 */
sealed interface AccessResult<out T> {
    data class Ok<T>(val value: T) : AccessResult<T>
    data class Err(val reason: String, val cause: Throwable? = null) : AccessResult<Nothing>
}
