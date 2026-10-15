package com.ghostlock.app.data.profile

import android.content.res.AssetManager

/**
 * 把 AssetManager 接到画像装载面上。
 *
 * 资产的根是 assets/profile/：profile 名就是文件名，include 里的相对路径也在这个目录里解析。
 * 读不到就返回 null，由 DeviceProfile 给具名理由 —— 这一层不吞错、不返回空串。
 */
class AndroidAssetSource(private val assets: AssetManager) : AssetSource, AssetLister {
    override fun text(name: String): String? = try {
        assets.open(ROOT + name).use { stream -> stream.readBytes().toString(Charsets.UTF_8) }
    } catch (missing: java.io.IOException) {
        null
    }

    override fun names(): List<String> = try {
        assets.list(ROOT)?.toList().orEmpty()
    } catch (missing: java.io.IOException) {
        emptyList()
    }

    private companion object {
        /** 画像资产的目录。 */
        const val ROOT = "profile/"
    }
}
