package com.ghostlock.app.backend.cve98260.client

import android.content.Context
import com.ghostlock.app.backend.cve98260.run.Cve98260SafetyPort
import com.ghostlock.app.data.cve98260.BootSafetyStore
import java.io.File

/**
 * app 侧安全闸适配器（B2）：把 [Cve98260SafetyPort] 落到唯一权威 [BootSafetyStore]。
 *
 * @param context 应用上下文（仅用于 SharedPreferences）。
 * @note app 直读 `/proc/sys/kernel/random/boot_id` 为权威；权限未核实（TD-19），
 *   失败时 Flow 走 AIDL `bootId()` 回退。
 */
internal class Cve98260Safety(context: Context) : Cve98260SafetyPort {
    private val appContext: Context = context.applicationContext

    override fun kernelRelease(): String? =
        runCatching { System.getProperty("os.version") }.getOrNull()?.trim()?.ifEmpty { null }

    override fun bootId(): String? = runCatching {
        File("/proc/sys/kernel/random/boot_id").readText().trim().ifEmpty { null }
    }.getOrNull()

    override fun storeBlocked(): Boolean = BootSafetyStore.load(appContext).isBlocked()

    override fun markLaunchPending(bootId: String?): Boolean =
        BootSafetyStore.markLaunchPending(appContext, bootId)

    override fun markTerminal(
        bootId: String?,
        mustReboot: Boolean,
        outcome: String,
        reason: String?,
    ): Boolean = BootSafetyStore.markTerminal(appContext, bootId, mustReboot, outcome, reason)
}
