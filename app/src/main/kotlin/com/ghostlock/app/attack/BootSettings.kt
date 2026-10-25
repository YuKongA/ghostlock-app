package com.ghostlock.app.attack

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** 开机执行与执行后动作的 UI 偏好；当前不连接接收器或执行服务。 */
object BootSettings {
    private const val PREFS = "ghostlock.boot"
    private const val KEY_AUTO = "auto_attack"
    private const val KEY_RETRY = "boot_retry_count"
    private const val KEY_REBOOT = "auto_soft_reboot"

    var autoAttackOnBoot by mutableStateOf(false)
        private set

    var bootRetryCount by mutableIntStateOf(1)
        private set

    var autoSoftRebootAfterAttack by mutableStateOf(false)
        private set

    /** 从应用首选项加载三个设置控件的状态。 */
    fun attach(context: Context) {
        val prefs = context.getSharedPreferences(PREFS, 0)
        autoAttackOnBoot = prefs.getBoolean(KEY_AUTO, false)
        bootRetryCount = prefs.getInt(KEY_RETRY, 1).coerceIn(0, 10)
        autoSoftRebootAfterAttack = prefs.getBoolean(KEY_REBOOT, false)
    }

    /** 保存开机自动执行开关；当前仅保存 UI 偏好。 */
    fun setAutoAttackOnBoot(context: Context, value: Boolean) {
        autoAttackOnBoot = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_AUTO, value).apply()
    }

    /** 保存开机重试次数，并把输入限制在 0 到 10。 */
    fun setBootRetryCount(context: Context, value: Int) {
        bootRetryCount = value.coerceIn(0, 10)
        context.getSharedPreferences(PREFS, 0).edit().putInt(KEY_RETRY, bootRetryCount).apply()
    }

    /** 保存执行后软重启开关；当前仅保存 UI 偏好。 */
    fun setAutoSoftRebootAfterAttack(context: Context, value: Boolean) {
        autoSoftRebootAfterAttack = value
        context.getSharedPreferences(PREFS, 0).edit().putBoolean(KEY_REBOOT, value).apply()
    }
}
