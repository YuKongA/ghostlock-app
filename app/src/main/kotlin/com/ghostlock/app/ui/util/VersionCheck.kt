package com.ghostlock.app.ui.util

import com.ghostlock.app.ui.util.module.LatestVersionInfo

/**
 * 检查管理器有没有新版本（未接线：不访问网络）。
 *
 * 上游实现在 Downloader.kt 里拉 GitHub release；下载/联网层随裁剪移除后，
 * 这里只返回"没有新版本"的固定样例。
 *
 * @return 恒为 [LatestVersionInfo] 的默认值。
 */
fun checkNewVersion(): LatestVersionInfo = LatestVersionInfo()
