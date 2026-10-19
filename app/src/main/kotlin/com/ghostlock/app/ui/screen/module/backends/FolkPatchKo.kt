package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.system.Os
import java.io.File

/**
 * 从已安装的 FolkPatch APK 里自动提取当前内核对应的 kernelpatch.ko。
 *
 * FolkPatch 把预构建模块放在 assets/<kmi>_kernelpatch.ko；
 * 这里按 uname -r 解析 KMI，复制到应用私有目录后交给收尾脚本加载。
 */
object FolkPatchKo {
    private val PACKAGES = listOf("me.yuki.folk", "me.bmax.apatch")
    private val KMI_PATTERN = Regex("(.* )?(\\d+\\.\\d+)(\\S+)?(android\\d+)(.*)")

    /** 从 uname -r 解析 KMI，例如 android14-5.15；解析失败返回 null。 */
    fun parseKmi(release: String): String? {
        val match = KMI_PATTERN.find(release) ?: return null
        return match.groupValues[4] + "-" + match.groupValues[2]
    }

    /** 当前内核的 KMI；解析失败返回 null。 */
    fun currentKmi(): String? = runCatching { parseKmi(Os.uname().release) }.getOrNull()

    /** 提取 ko 到 files/finish/folkpatch.ko，返回绝对路径；找不到返回 null。 */
    fun extract(context: Context): String? {
        val kmi = currentKmi()
        for (pkg in PACKAGES) {
            val info = runCatching { context.packageManager.getApplicationInfo(pkg, 0) }.getOrNull() ?: continue
            val assets = runCatching { context.packageManager.getResourcesForApplication(info).assets }.getOrNull() ?: continue
            val names = runCatching { assets.list("")?.toList().orEmpty() }.getOrDefault(emptyList())
            val candidates = names.filter { it.endsWith("_kernelpatch.ko") }
            val name = candidates.firstOrNull { kmi != null && it == kmi + "_kernelpatch.ko" }
                ?: candidates.firstOrNull()
                ?: continue
            /* 放到应用外部目录：root 域能读，SELinux 不会因 app_data_file 拒绝 insmod/读取。 */
            val dir = context.getExternalFilesDir(null)?.resolve("finish")
                ?: File(context.filesDir, "finish")
            val dest = File(dir, "folkpatch.ko").apply { parentFile?.mkdirs() }
            val copied = runCatching {
                assets.open(name).use { input -> dest.outputStream().use { output -> input.copyTo(output) } }
                if (dest.isFile && dest.length() > 0L) dest.absolutePath else null
            }.getOrNull()
            if (copied != null) return copied
        }
        return null
    }
}
