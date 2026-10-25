package com.ghostlock.app.core.access

/** 已准备 KernelMemoryAccess 上的一次读取请求；纯数据，不在这里校验。 */
data class KernelReadRequest(
    val address: ULong,
    val length: Int,
)

/** 已准备 KernelMemoryAccess 上的一次写入请求；纯数据，不在这里校验。 */
data class KernelWriteRequest(
    val address: ULong,
    val bytes: ByteArray,
)

/** 已准备 PageCacheAccess 上的一次读取请求；target 已绑定在 capability 中。 */
data class PageCacheReadRequest(
    val offset: ULong,
    val length: Int,
)

/** 已准备 PageCacheAccess 上的一次写入请求；target 已绑定在 capability 中。 */
data class PageCacheWriteRequest(
    val offset: ULong,
    val bytes: ByteArray,
)

/** Kotlin 数据面使用的写入回执；不暴露 Native handle，也不携带视图代际。 */
data class AccessWriteReceipt(
    val bytesWritten: Int,
)
