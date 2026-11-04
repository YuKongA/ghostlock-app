package com.ghostlock.app.data.backend

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendSpec

/**
 * 解析持久化的 backend 选择（纯函数；B4 单测用）。
 *
 * 解析顺序（单一选择权威=[BackendSpec.selectable]）：
 * 1. **exact key / exact cve** 优先；
 * 2. 仅当 exact 缺失时，才用**具名 legacy cve 前缀**一次性兼容（旧版本 prefs 保存
 *    `CVE-2026-43499`，而当前 cve 为 `CVE-2026-43499-Glued`），且必须唯一。
 *
 * @param specs composition root 捕获的当前注册表快照。
 * @param saved 持久化值（可为旧 CVE 前缀）。
 * @return 解析到的 selectable spec；无匹配/歧义/不可选返回 null（fail-closed）。
 * @note legacy 前缀只做迁移，不构成第二选择权威；调用方恢复后应写回 canonical key。
 */
internal fun resolveStoredSpec(specs: List<BackendSpec>, saved: String): BackendSpec? {
    if (saved.isBlank()) return null
    val exact = specs.firstOrNull { it.key == saved || it.cve == saved }
    if (exact != null) return exact.takeIf { it.selectable }
    val legacyMatches = specs.filter { spec ->
        spec.selectable && spec.cve.startsWith("$saved-")
    }
    return legacyMatches.singleOrNull()
}

/** Process-local backend-selection state owned by one [BackendSessionStore] instance. */
internal class BackendSessionSelection {
    /** Current stable key after fallback, restore, or explicit selection. */
    var selectedKey: String? = null
        private set

    /**
     * Resolves persisted input and always establishes the captured selectable fallback.
     *
     * @param specs Captured registry membership in display order.
     * @param saved Persisted stable or legacy key; null means no stored selection.
     * @return Restored spec, or null when fallback was used.
     */
    fun restore(specs: List<BackendSpec>, saved: String?): BackendSpec? {
        val restored = saved?.let { value -> resolveStoredSpec(specs, value) }
        selectedKey = (restored ?: specs.firstOrNull { it.selectable })?.key
        return restored
    }

    /**
     * Selects one already-resolved captured backend.
     *
     * @param spec Candidate backend.
     * @return True only when the candidate is selectable and became authoritative.
     */
    fun select(spec: BackendSpec): Boolean {
        if (!spec.selectable) return false
        selectedKey = spec.key
        return true
    }
}

/**
 * backend 会话（上次选择 + 配置恢复）的 application 生命周期存储。
 *
 * 职责（task-27）：
 * - 持久化 `selected_cve` 字段（值为稳定 backend key；兼容旧版本 CVE 值）；
 * - `restore(context, specs)`：解析 key → selectable spec，写实例级 [selectedKey]，
 *   然后调用该 backend 自己的 `restoreConfig(store)`（**具体内容由 backend 提供**）；
 * - `select(context, spec)`：保存 `selected_cve`，并对选中 backend 做一次
 *   restore→save 往返（确保切到该 backend 时加载它自己的持久化配置，并保持落盘一致）；
 * - 向 backend 提供 [BackendConfigStore]（命名空间/类型/白名单由 store 强制）。
 *
 * 顺序约束：
 * - 启动恢复必须在 `setContent` 之前（MainActivity.onCreate）执行，保证首帧读到正确选择；
 * - 无法解析的 `selected_cve`（旧版本/损坏）⇒ 保持默认下标 0 并记日志（fail-closed，不崩）；
 * - backend 的 restoreConfig 异常 ⇒ 记录日志并保持该 backend 自身默认（fail-closed）。
 *
 * @note 无全局可变状态：composition root 持有唯一实例并注入所有选择/读取 adapter。
 * @note 阶段：task-27；真机未验证，不构成 supported 宣称。
 */
class BackendSessionStore private constructor(
    private val prefs: SharedPreferences,
) {
    private val selection = BackendSessionSelection()

    /** 当前 composition-root 会话的稳定 backend key。 */
    val selectedKey: String?
        get() = selection.selectedKey

    /** @return 上次保存的稳定 key 或 legacy CVE 值；未保存/空串返回 null。 */
    private fun storedSelection(): String? =
        prefs.getString(KEY_SELECTED_CVE, null)?.takeIf { it.isNotBlank() }

    /**
     * 恢复上次选择的后端及其配置（启动时调用，须在 setContent 前）。
     *
     * @param context 应用上下文。
     * @return 被恢复的 backend；没有保存记录/无法解析时返回 null（选择保持默认 index 0）。
     * @note 重复调用幂等；backend restoreConfig 异常只会丢该 backend 的配置，不影响选择。
     */
    fun restore(context: Context, specs: List<BackendSpec>): BackendSpec? {
        val saved = storedSelection()
        val restored = selection.restore(specs, saved)
        if (saved == null) return null
        if (restored == null) {
            Log.w(TAG, "saved backend is unavailable: " + saved)
            return null
        }
        val spec = restored
        if (saved != spec.key) {
            /* 具名 legacy 迁移（一次性）：旧 prefs 的 CVE 值恢复成功后写回 canonical key。 */
            runCatching { prefs.edit().putString(KEY_SELECTED_CVE, spec.key).commit() }
                .onFailure { e -> Log.w(TAG, "legacy key canonicalization failed: " + saved, e) }
        }
        runCatching { spec.restoreConfig(configStore(context, spec)) }
            .onFailure { e -> Log.w(TAG, "restoreConfig failed: " + spec.cve, e) }
        return spec
    }

    /**
     * 保存当前选择的后端，并同步持久化/加载该后端的配置内容。
     *
     * @param context 应用上下文。
     * @param spec composition root 捕获且已按稳定 key 解析的 backend。
     * @note 先 restore 再 save：切到某 backend 时先加载它自己的持久化配置，避免用默认值覆盖。
     */
    fun select(context: Context, spec: BackendSpec) {
        if (!selection.select(spec)) {
            Log.w(TAG, "select ignored, backend is disabled: " + spec.key)
            return
        }
        if (!prefs.edit().putString(KEY_SELECTED_CVE, spec.key).commit()) {
            Log.w(TAG, "failed to persist selected backend: " + spec.cve)
        }
        val store = configStore(context, spec)
        runCatching { spec.restoreConfig(store) }
            .onFailure { e -> Log.w(TAG, "restoreConfig failed on select: " + spec.cve, e) }
        runCatching { spec.saveConfig(store) }
            .onFailure { e -> Log.w(TAG, "saveConfig failed on select: " + spec.cve, e) }
    }

    /**
     * @param context 应用上下文。
     * @param spec 目标 backend。
     * @return 该 backend 的配置 store（data adapter 创建并注入，backend 不得直接碰 SharedPreferences）。
     */
    fun configStore(context: Context, spec: BackendSpec): BackendConfigStore =
        SharedPreferencesBackendConfigStore.open(context, spec.cve, spec.configSchema)

    companion object {
        private const val TAG = "BackendSessionStore"

        /** 会话 prefs 文件名。 */
        private const val PREFS = "ghostlock.backend.session"

        /** 上次选择的后端 CVE key。 */
        private const val KEY_SELECTED_CVE = "selected_cve"

        /**
         * @param context 应用上下文。
         * @return 轻量 session store 实例（由 composition root 持有；无全局可变状态）。
         */
        fun from(context: Context): BackendSessionStore {
            val app = context.applicationContext
            return BackendSessionStore(app.getSharedPreferences(PREFS, Context.MODE_PRIVATE))
        }
    }
}
