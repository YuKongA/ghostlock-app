package com.ghostlock.app.core.access

/** 关闭回执；clean 表示 native 资源是否无错误释放。 */
data class ShutdownReceipt(
    val clean: Boolean,
)
