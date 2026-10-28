package com.ghostlock.app.data.cve98260

import android.content.Context
import org.json.JSONObject

/**
 * CVE-2026-98260 的跨 boot 安全闸（B2）。
 *
 * **唯一权威**（delta §1.1/§2）：`must_reboot`/`launch_pending` 只从本 store 读取；
 * Flow/UI 的证据字段只是镜像。字段：
 * - `boot_id`：当前启动标识（app 直读为权威；失败回退 AIDL `bootId()`，见 TD-19）；
 * - `must_reboot`：armed/park/不确定后置 true，**成功(PARKED)也置 true**；按 boot_id 失效；
 * - `launch_pending`：AIDL `start()` 前先落盘，app 中途死亡也不失闸；
 * - `outcome/reason/updated_ms`：审计与具名终态。
 *
 * 写路径统一 `commit()`（同步）并校验返回值；失败语义由调用方按"未 armed/already armed"处理。
 * 不保存密钥/令牌。
 *
 * @note 阶段：B2；真机门禁 G8–G13 未过。
 */
internal object BootSafetyStore {
    private const val PREFS = "ghostlock.cve98260.safety"
    private const val KEY = "state"
    private const val FORMAT_VERSION = 1

    /** 一次读取的不可变快照。 */
    internal data class Snapshot(
        val bootId: String?,
        val mustReboot: Boolean,
        val launchPending: Boolean,
        val outcome: String?,
        val reason: String?,
        val updatedMs: Long,
    ) {
        /** @return true=本 boot 不允许再次运行（任一安全闸置位）。 */
        internal fun isBlocked(): Boolean = mustReboot || launchPending
    }

    /** 空快照（无 prefs/损坏）。 */
    internal val EMPTY: Snapshot = Snapshot(null, false, false, null, null, 0L)

    /** @return 当前持久化快照；损坏/缺失返回 [EMPTY]（fail-closed 由调用方按 boot 判定）。 */
    internal fun load(context: Context): Snapshot {
        val raw = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY, null) ?: return EMPTY
        return runCatching {
            val json = JSONObject(raw)
            if (json.optInt("schemaVersion", 0) != FORMAT_VERSION) return@runCatching EMPTY
            Snapshot(
                bootId = json.optString("bootId", "").takeIf { it.isNotEmpty() },
                mustReboot = json.optBoolean("mustReboot", false),
                launchPending = json.optBoolean("launchPending", false),
                outcome = json.optString("outcome", "").takeIf { it.isNotEmpty() },
                reason = json.optString("reason", "").takeIf { it.isNotEmpty() },
                updatedMs = json.optLong("updatedMs", 0L),
            )
        }.getOrDefault(EMPTY)
    }

    /**
     * 在 AIDL `start()` 前落 `launch_pending=true`。
     *
     * @param context 应用上下文。
     * @param bootId 当前 boot_id（未知时传 null；此时调用方应已 Refused）。
     * @return true=持久化成功；false=commit 失败（调用方不得 launch）。
     */
    internal fun markLaunchPending(context: Context, bootId: String?): Boolean =
        write(context, Snapshot(bootId, false, true, "launching", null, System.currentTimeMillis()))

    /**
     * 写终态（成功必须 `mustReboot=true`，见 D6）。
     *
     * @param context 应用上下文。
     * @param bootId 当前 boot_id。
     * @param mustReboot 本 boot 是否禁止再次运行（PARKED/不确定/armed 失败=true）。
     * @param outcome 具名终态（`ok`/`failed`/`indeterminate`/`persist-failed`）。
     * @param reason 具名 reason（可空）。
     * @return true=持久化成功。
     */
    internal fun markTerminal(
        context: Context,
        bootId: String?,
        mustReboot: Boolean,
        outcome: String,
        reason: String?,
    ): Boolean = write(
        context,
        Snapshot(bootId, mustReboot, false, outcome, reason, System.currentTimeMillis()),
    )

    /**
     * 仅当本 boot 无闸或 boot_id 变化时清除。
     *
     * @param context 应用上下文。
     * @param currentBootId 当前 boot_id。
     * @return true=已清除/无需清除；false=boot_id 未变且存在安全闸（拒绝清除）。
     */
    internal fun clearIfBootChanged(context: Context, currentBootId: String?): Boolean {
        val snapshot = load(context)
        if (snapshot.bootId != null && currentBootId != null && snapshot.bootId == currentBootId &&
            snapshot.isBlocked()
        ) {
            return false
        }
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .remove(KEY).commit()
    }

    /** 原子写（commit + 返回值；JSON 固定 schema）。 */
    private fun write(context: Context, snapshot: Snapshot): Boolean {
        val json = JSONObject().apply {
            put("schemaVersion", FORMAT_VERSION)
            put("bootId", snapshot.bootId ?: "")
            put("mustReboot", snapshot.mustReboot)
            put("launchPending", snapshot.launchPending)
            put("outcome", snapshot.outcome ?: "")
            put("reason", snapshot.reason ?: "")
            put("updatedMs", snapshot.updatedMs)
        }
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(KEY, json.toString()).commit()
    }
}
