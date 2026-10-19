package com.ghostlock.app.attack

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** 攻击执行相关设置（是否在重新执行前警告）。 */
object AttackSettings {
    private const val PREFS = "ghostlock.attack"
    private const val KEY_WARN = "warn_on_rerun"

    var warnOnRerun by mutableStateOf(true)
        private set

    fun attach(context: Context) {
        warnOnRerun = context.getSharedPreferences(PREFS, 0).getBoolean(KEY_WARN, true)
    }

    fun setWarnOnRerun(context: Context, value: Boolean) {
        warnOnRerun = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_WARN, value).apply()
    }
}
