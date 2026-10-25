package com.ghostlock.app.core.access

/**
 * 能力借用视图标记。
 *
 * 视图非拥有、永不 close、不带 generation；生命周期与 stale 判据属于
 * [AccessSession]（或持有 backend handle 的 RunResourceScope）和 native。
 * 视图只在 owner 已就绪时发布；owner 关闭后不得继续使用旧视图。
 * 视图实现由 owner 构造并私有持有 owner/borrow；request 只放操作数据，
 * 不放 handle；access 实现不做 lazy initialize。
 */
interface AccessCapability
