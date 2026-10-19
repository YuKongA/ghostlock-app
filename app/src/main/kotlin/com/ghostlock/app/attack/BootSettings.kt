package com.ghostlock.app.attack

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File

/** 开机自动攻击与攻击后动作的设置。 */
object BootSettings {
    private const val PREFS = "ghostlock.boot"
    private const val KEY_AUTO = "auto_attack"
    private const val KEY_RETRY = "boot_retry_count"
    private const val KEY_REBOOT = "auto_soft_reboot"
    const val ATTEMPT_FILE = "boot-attempt"

    var autoAttackOnBoot by mutableStateOf(false)
        private set

    var bootRetryCount by mutableIntStateOf(1)
        private set

    var autoSoftRebootAfterAttack by mutableStateOf(false)
        private set

    fun attach(context: Context) {
        val prefs = context.getSharedPreferences(PREFS, 0)
        autoAttackOnBoot = prefs.getBoolean(KEY_AUTO, false)
        bootRetryCount = prefs.getInt(KEY_RETRY, 1).coerceIn(0, 10)
        autoSoftRebootAfterAttack = prefs.getBoolean(KEY_REBOOT, false)
    }

    fun setAutoAttackOnBoot(context: Context, value: Boolean) {
        autoAttackOnBoot = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_AUTO, value).apply()
    }

    fun setBootRetryCount(context: Context, value: Int) {
        bootRetryCount = value.coerceIn(0, 10)
        context.getSharedPreferences(PREFS, 0).edit().putInt(KEY_RETRY, bootRetryCount).apply()
    }

    fun setAutoSoftRebootAfterAttack(context: Context, value: Boolean) {
        autoSoftRebootAfterAttack = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_REBOOT, value).apply()
    }

    /** 开机尝试计数：panic 时来不及清零，下次开机据此判断“上一轮没跑完”。 */
    fun attempts(context: Context): Int =
        runCatching { File(context.filesDir, ATTEMPT_FILE).readText().trim().toInt() }.getOrDefault(0)

    fun recordAttempt(context: Context, value: Int) {
        runCatching { File(context.filesDir, ATTEMPT_FILE).writeText(value.toString()) }
    }

    fun clearAttempts(context: Context) {
        runCatching { File(context.filesDir, ATTEMPT_FILE).delete() }
    }
}
