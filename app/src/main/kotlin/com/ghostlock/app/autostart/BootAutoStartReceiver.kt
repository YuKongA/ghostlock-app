package com.ghostlock.app.autostart

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/**
 * Launches the headless exploit run on boot. BOOT_COMPLETED is deliberately
 * the earliest usable trigger: it is the first point where credential-encrypted
 * storage (the profiles, the persisted selection) and the full framework
 * (Shizuku) are available to this app; LOCKED_BOOT_COMPLETED would run against
 * locked CE storage. Repeated boot broadcasts within one boot are deduped by
 * the [BootGuard] attempt bookkeeping.
 *
 * OEM auto-start management (Honor/Huawei PowerGenie and the like) can silently
 * drop the boot broadcast for unlisted apps; the decision is always logged so a
 * captured logcat answers "did the receiver even run".
 */
class BootAutoStartReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val decision = BootGuard.of(context).onBoot()
        Log.i(TAG, "${intent.action} -> $decision")
        when (decision) {
            BootDecision.RUN -> AutoStartService.scheduleRun(context)
            BootDecision.GUARD_TRIPPED -> AutostartNotifications.postGuardTripped(context)
            BootDecision.DISABLED,
            BootDecision.ALREADY_HANDLED,
            -> Unit
        }
    }

    private companion object {
        const val TAG = "GhostLockAutostart"
    }
}
