package com.ghostlock.app.core.access

/** 初始化回执；generation 用于判定能力视图是否过期。 */
data class InitializationReceipt(
    val generation: Long,
    val carrierPath: String? = null,
)
