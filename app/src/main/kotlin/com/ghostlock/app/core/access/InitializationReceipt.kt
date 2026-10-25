package com.ghostlock.app.core.access

/** 初始化回执；generation 是会话元数据，不是能力视图代际。 */
data class InitializationReceipt(
    val generation: Long,
    val carrierPath: String? = null,
)
