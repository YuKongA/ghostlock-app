package com.ghostlock.app.ui.screen.module.backends

import android.system.Os

/**
 * 从已安装的 FolkPatch APK 里取出当前内核对应的 kernelpatch.ko。
 *
 * FolkPatch 把预构建模块放在 assets/<kmi>_kernelpatch.ko；这里按 uname -r 解析 KMI，
 * 把字节读进 memfd（`/proc/<pid>/fd/<n>`），不再落盘。
 */
object FolkPatchKo {
    private val KMI_PATTERN = Regex("(.* )?(\\d+\\.\\d+)(\\S+)?(android\\d+)(.*)")
    private val fallbackByKernel = mapOf(
        "5.10" to "android12-5.10",
        "5.15" to "android13-5.15",
        "6.1" to "android14-6.1",
        "6.6" to "android15-6.6",
        "6.12" to "android16-6.12",
        "6.18" to "android17-6.18",
    )

    /** 从 uname -r 解析 KMI；没有 android 标记时按内核版本回退。 */
    fun parseKmi(release: String): String? {
        val match = KMI_PATTERN.find(release)
        if (match != null) {
            return match.groupValues[4] + "-" + match.groupValues[2]
        }
        val kernel = Regex("(\\d+)\\.(\\d+)").find(release) ?: return null
        return fallbackByKernel[kernel.groupValues[1] + "." + kernel.groupValues[2]]
    }

    /** 当前内核的 KMI；解析失败返回 null。 */
    fun currentKmi(): String? = runCatching { parseKmi(Os.uname().release) }.getOrNull()
}
