package com.ghostlock.app.ui.kit

import android.content.Context

/**
 * 套件选择落在 SharedPreferences 上：一个键，两个取值，写立刻可见（apply 之后同进程的下一次
 * 读就是新值，UI 在同一进程里刷新）。
 *
 * 键名与取值都写在这里，别处不拼字符串：拼错一个键的表现是"切换没反应"，最难查的那种坏。
 */
class AndroidUiKitStore(context: Context) : UiKitStore {
    private val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    override fun read(): UiKit =
        when (prefs.getString(KEY_KIT, null)) {
            VALUE_MIUIX -> UiKit.Miuix
            else -> UiKit.Material
        }

    override fun write(kit: UiKit) {
        prefs.edit().putString(KEY_KIT, if (kit == UiKit.Miuix) VALUE_MIUIX else VALUE_MATERIAL).apply()
    }

    private companion object {
        const val PREFS = "ghostlock.ui"
        const val KEY_KIT = "ui.kit"
        const val VALUE_MATERIAL = "material"
        const val VALUE_MIUIX = "miuix"
    }
}
