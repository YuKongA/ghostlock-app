package com.ghostlock.app.attack

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** 执行页的交互偏好；只保存 UI 设置，不连接执行层。 */
object AttackSettings {
    private const val PREFS = "ghostlock.attack"
    private const val KEY_WARN = "warn_on_rerun"

    var warnOnRerun by mutableStateOf(true)
        private set

    /** 从应用首选项加载 UI 状态。 */
    fun attach(context: Context) {
        warnOnRerun = context.getSharedPreferences(PREFS, 0).getBoolean(KEY_WARN, true)
    }

    /** 保存重新执行前是否显示确认框。 */
    fun setWarnOnRerun(context: Context, value: Boolean) {
        warnOnRerun = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_WARN, value).apply()
    }
}
