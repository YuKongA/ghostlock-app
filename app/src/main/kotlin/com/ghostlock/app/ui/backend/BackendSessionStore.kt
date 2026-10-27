package com.ghostlock.app.ui.backend

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.ghostlock.app.ui.screen.module.backends.BackendSelection
import com.ghostlock.app.ui.screen.module.backends.BackendSpec
import com.ghostlock.app.ui.screen.module.backends.Backends

/**
 * backend 会话（上次选择 + 配置恢复）的 UI 框架生命周期存储。
 *
 * 职责（task-27）：
 * - 持久化 `selected_cve`（上次启动时选择的后端）；
 * - `restore(context)`：解析 cve → `Backends.all` 下标，写 [BackendSelection.index]，
 *   然后调用该 backend 自己的 `restoreConfig(store)`（**具体内容由 backend 提供**）；
 * - `select(context, index)`：保存 `selected_cve`，并对选中 backend 做一次
 *   restore→save 往返（确保切到该 backend 时加载它自己的持久化配置，并保持落盘一致）；
 * - 向 backend 提供 [BackendConfigStore]（命名空间/类型/白名单由 store 强制）。
 *
 * 顺序约束：
 * - 启动恢复必须在 `setContent` 之前（MainActivity.onCreate）执行，保证首帧读到正确选择；
 * - 无法解析的 `selected_cve`（旧版本/损坏）⇒ 保持默认下标 0 并记日志（fail-closed，不崩）；
 * - backend 的 restoreConfig 异常 ⇒ 记录日志并保持该 backend 自身默认（fail-closed）。
 *
 * @note 无全局可变状态：每次 `from(context)` 创建轻量实例，由 UI 层持有/调用。
 * @note 阶段：task-27；真机未验证，不构成 supported 宣称。
 */
class BackendSessionStore private constructor(
    private val prefs: SharedPreferences,
) {
    /** @return 上次保存的后端 CVE；未保存/空串返回 null。 */
    fun selectedCve(): String? = prefs.getString(KEY_SELECTED_CVE, null)?.takeIf { it.isNotBlank() }

    /**
     * 恢复上次选择的后端及其配置（启动时调用，须在 setContent 前）。
     *
     * @param context 应用上下文。
     * @return 被恢复的 backend；没有保存记录/无法解析时返回 null（选择保持默认 index 0）。
     * @note 重复调用幂等；backend restoreConfig 异常只会丢该 backend 的配置，不影响选择。
     */
    fun restore(context: Context): BackendSpec? {
        val cve = selectedCve() ?: return null
        val index = Backends.all.indexOfFirst { it.cve == cve }
        if (index < 0) {
            Log.w(TAG, "saved backend no longer registered: " + cve)
            BackendSelection.index = 0
            return null
        }
        BackendSelection.index = index
        val spec = Backends.all[index]
        runCatching { spec.restoreConfig(configStore(context, spec)) }
            .onFailure { e -> Log.w(TAG, "restoreConfig failed: " + spec.cve, e) }
        return spec
    }

    /**
     * 保存当前选择的后端，并同步持久化/加载该后端的配置内容。
     *
     * @param context 应用上下文。
     * @param index `Backends.all` 下标；越界 ⇒ 忽略并记日志（fail-closed）。
     * @note 先 restore 再 save：切到某 backend 时先加载它自己的持久化配置，避免用默认值覆盖。
     */
    fun select(context: Context, index: Int) {
        val spec = Backends.all.getOrNull(index)
        if (spec == null) {
            Log.w(TAG, "select ignored, bad backend index: " + index)
            return
        }
        if (!prefs.edit().putString(KEY_SELECTED_CVE, spec.cve).commit()) {
            Log.w(TAG, "failed to persist selected backend: " + spec.cve)
        }
        BackendSelection.index = index
        val store = configStore(context, spec)
        runCatching { spec.restoreConfig(store) }
            .onFailure { e -> Log.w(TAG, "restoreConfig failed on select: " + spec.cve, e) }
        runCatching { spec.saveConfig(store) }
            .onFailure { e -> Log.w(TAG, "saveConfig failed on select: " + spec.cve, e) }
    }

    /**
     * @param context 应用上下文。
     * @param spec 目标 backend。
     * @return 该 backend 的配置 store（UI 框架创建并注入，backend 不得直接碰 SharedPreferences）。
     */
    fun configStore(context: Context, spec: BackendSpec): BackendConfigStore =
        BackendConfigStore.open(context, spec.cve, spec.configSchema)

    companion object {
        private const val TAG = "BackendSessionStore"

        /** 会话 prefs 文件名。 */
        private const val PREFS = "ghostlock.backend.session"

        /** 上次选择的后端 CVE key。 */
        private const val KEY_SELECTED_CVE = "selected_cve"

        /**
         * @param context 应用上下文。
         * @return 轻量 session store 实例（由 UI 层持有；无全局可变状态）。
         */
        fun from(context: Context): BackendSessionStore {
            val app = context.applicationContext
            return BackendSessionStore(app.getSharedPreferences(PREFS, Context.MODE_PRIVATE))
        }
    }
}
