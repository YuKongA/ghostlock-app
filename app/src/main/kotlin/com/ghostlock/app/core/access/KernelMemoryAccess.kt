package com.ghostlock.app.core.access

/**
 * 已完成 backend 准备后的内存读写借用视图。
 *
 * 非拥有、永不 close、无 generation；stale 由 owner/native 在调用边界拒绝。
 * 本接口不负责 profile、地址计算、扫描、资源准备、route 或重试。
 */
interface KernelMemoryAccess : AccessCapability {
    suspend fun read(request: KernelReadRequest): AccessResult<ByteArray>

    suspend fun write(request: KernelWriteRequest): AccessResult<AccessWriteReceipt>
}
