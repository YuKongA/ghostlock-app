package com.ghostlock.app.core.access

/** 一个会话暴露的能力；缺席用 null，不给假实现。 */
data class AccessCapabilities(
    val kernelMemory: KernelMemoryAccess? = null,
    val pageCache: PageCacheAccess? = null,
)
