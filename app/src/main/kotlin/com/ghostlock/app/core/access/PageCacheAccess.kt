package com.ghostlock.app.core.access

/**
 * 已完成 backend 准备后的页缓存读写借用视图。
 *
 * 非拥有、永不 close、无 generation；stale 由 owner/native 在调用边界拒绝。
 * 本接口不负责 carrier 选择、页准备、流程编排或 handoff。
 */
interface PageCacheAccess : AccessCapability {
    suspend fun read(request: PageCacheReadRequest): AccessResult<ByteArray>

    suspend fun write(request: PageCacheWriteRequest): AccessResult<AccessWriteReceipt>
}
