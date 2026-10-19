package com.ghostlock.app.attack

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * 开机自动攻击。
 *
 * 重试次数写盘：每次尝试前把计数写入 files/boot-attempt；
 * 正常跑完由 AttackService 清零。若上次尝试中 panic/重启，计数仍在，
 * 下次开机就据此判断“上一轮没跑完”，在配置次数内继续尝试。
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (intent?.action != Intent.ACTION_BOOT_COMPLETED) return
        BootSettings.attach(context)
        if (!BootSettings.autoAttackOnBoot) return
        val done = BootSettings.attempts(context)
        if (done >= BootSettings.bootRetryCount) {
            BootSettings.clearAttempts(context)
            return
        }
        BootSettings.recordAttempt(context, done + 1)
        context.startForegroundService(
            Intent(context, AttackService::class.java).setAction(AttackService.ACTION_START),
        )
    }
}
