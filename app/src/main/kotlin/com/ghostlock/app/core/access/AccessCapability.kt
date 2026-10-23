package com.ghostlock.app.core.access

/**
 * 能力视图标记。
 *
 * 视图非拥有、永不 close；会话换代后旧 generation 一律失效。
 */
interface AccessCapability {
    val generation: Long
}
