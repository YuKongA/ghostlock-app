package com.ghostlock.app.engine.pagecache

import android.content.Context
import android.system.Os

/**
 * 按当前内核选择 assets/lkm/<label>/ghostlock.ko。
 *
 * 有 android<N> 的内核用 android<N>-<major>.<minor>；没有 android 标记的
 * 5.10 按 android12-5.10 处理（用户裁定：不要按本机 Android 版本猜）。
 */
object LkmSelector {
    private val fallbackByKernel = mapOf(
        "5.10" to "android12-5.10",
        "5.15" to "android13-5.15",
        "6.1" to "android14-6.1",
        "6.6" to "android15-6.6",
        "6.12" to "android16-6.12",
        "6.18" to "android17-6.18",
    )

    fun label(release: String = Os.uname().release): String? {
        val match = Regex("(\\d+)\\.(\\d+)").find(release) ?: return null
        val kernel = match.groupValues[1] + "." + match.groupValues[2]
        val android = Regex("android(\\d+)").find(release)?.groupValues?.get(1)
        if (android != null) {
            val candidate = "android$android-$kernel"
            if (candidate in fallbackByKernel.values) return candidate
        }
        return fallbackByKernel[kernel]
    }

    /** 读取当前 KMI 的模块；asset 不存在返回 null，由调用方回退到 /data/local/tmp。 */
    fun read(context: Context): ByteArray? {
        val label = label() ?: return null
        return runCatching {
            context.assets.open("lkm/$label/ghostlock.ko").use { it.readBytes() }
        }.getOrNull()
    }
}
