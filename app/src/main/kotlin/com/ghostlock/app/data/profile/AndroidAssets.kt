package com.ghostlock.app.data.profile

import android.content.res.AssetManager

/**
 * 把 AssetManager 接到画像装载面上。
 *
 * 资产按 CVE 分目录：assets/profile/<cve-number>/（如 43284/、43499/，以后 64560/）。
 * profile 名就是文件名，include 里的相对路径也在同一个 CVE 目录里解析。
 * 读不到就返回 null，由 DeviceProfile 给具名理由 —— 这一层不吞错、不返回空串。
 */
class AndroidAssetSource(
    private val assets: AssetManager,
    private val root: String = ProfileRoots.DEFAULT,
) : AssetSource, AssetLister {
    override fun text(name: String): String? = try {
        val path = if (name.startsWith("common/") || name.startsWith("profile/")) name else root + name
        assets.open(path).use { stream -> stream.readBytes().toString(Charsets.UTF_8) }
    } catch (missing: java.io.IOException) {
        null
    }

    override fun names(): List<String> = try {
        assets.list(root)?.toList().orEmpty()
    } catch (missing: java.io.IOException) {
        emptyList()
    }

}

/** CVE 与资产子目录的映射：CVE-2026-43499 → assets/profile/43499/。 */
object ProfileRoots {
    const val DEFAULT = "profile/43499/"

    /** 由 CVE 编号得到资产根；未知格式回落到 [DEFAULT]。 */
    fun forCve(cve: String): String {
        val number = cve.substringAfterLast('-').filter { it.isDigit() }
        return if (number.isEmpty()) DEFAULT else "profile/" + number + "/"
    }

    /** 该后端是否有 profile 目录（43284 无需配置，返回 false）。 */
    fun hasProfiles(cve: String): Boolean = cve.substringAfterLast('-').filter { it.isDigit() } != "43284"
}
