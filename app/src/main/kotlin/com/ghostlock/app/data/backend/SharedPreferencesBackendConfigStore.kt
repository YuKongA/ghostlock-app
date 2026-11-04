package com.ghostlock.app.data.backend

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import com.ghostlock.app.backend.declarations.BackendConfigKey
import com.ghostlock.app.backend.declarations.BackendConfigSchema
import com.ghostlock.app.backend.declarations.BackendConfigStore
import com.ghostlock.app.backend.declarations.BackendConfigType
import com.ghostlock.app.backend.declarations.BackendConfigViolation

/**
 * backend 配置的 data-layer 持久化 store（SharedPreferences 后端）。
 *
 * 规则强制（全部 fail-closed，违反 ⇒ 抛 [BackendConfigViolation]）：
 * 1. **只接受 schema 声明的 key**：未声明的 get/put 直接拒绝；
 * 2. **类型必须与声明一致**：用错 typed get/put 拒绝；
 * 3. **命名空间由框架拼装**：实际 prefs key = `<cve>:<snake_case>`，backend 无法越界；
 * 4. **显式默认**：缺省值来自 schema 的 `defaultValue`，不使用 null/隐式默认；
 * 5. **损坏值 fail-closed**：存储值类型不符时清除该项并返回 schema 默认（记日志，不崩）。
 *
 * 约束：
 * - backend **不得**直接碰 SharedPreferences，只能经本 store；
 * - 禁止密钥/令牌/PII/大对象；key 必须稳定（改名=迁移风险）；
 * - restore 必须无副作用顺序依赖、重复调用幂等；
 * - store 由 composition root 的 data adapter 创建/持有，backend 只能通过 `saveConfig/restoreConfig` 参数获得。
 *
 * @property context 应用上下文（backend restore 需要写回自己的运行时配置）。
 * @property cve 所属 backend 的 CVE 编号（命名空间）。
 * @property schema 该 backend 声明的配置项。
 * @note 阶段：task-27；写用 `commit()`（同步）以保证 force-stop 后重启可读到最新值。
 */
internal class SharedPreferencesBackendConfigStore private constructor(
    private val prefs: SharedPreferences,
    override val context: Context,
    override val cve: String,
    override val schema: BackendConfigSchema,
) : BackendConfigStore {
    /** @return 声明项是否有已保存值。 */
    override fun contains(name: String): Boolean = prefs.contains(prefsKey(field(name)))

    /** @return BOOLEAN 配置值（未保存时返回 schema 默认）。 */
    override fun getBoolean(name: String): Boolean {
        val field = typed(name, BackendConfigType.BOOLEAN)
        return readOrDefault(field) {
            prefs.getBoolean(prefsKey(field), field.defaultValue as Boolean)
        }
    }

    /** @return INT 配置值（未保存时返回 schema 默认）。 */
    override fun getInt(name: String): Int {
        val field = typed(name, BackendConfigType.INT)
        return readOrDefault(field) {
            prefs.getInt(prefsKey(field), field.defaultValue as Int)
        }
    }

    /** @return LONG 配置值（未保存时返回 schema 默认）。 */
    override fun getLong(name: String): Long {
        val field = typed(name, BackendConfigType.LONG)
        return readOrDefault(field) {
            prefs.getLong(prefsKey(field), field.defaultValue as Long)
        }
    }

    /** @return STRING 配置值（未保存时返回 schema 默认；不含密钥/PII 由声明方保证）。 */
    override fun getString(name: String): String {
        val field = typed(name, BackendConfigType.STRING)
        val defaultValue = field.defaultValue as String
        return readOrDefault(field) {
            prefs.getString(prefsKey(field), defaultValue) ?: defaultValue
        }
    }

    /** 写入 BOOLEAN 配置值（同步 commit；未声明/类型不符 ⇒ 拒绝）。 */
    override fun putBoolean(name: String, value: Boolean) {
        val field = typed(name, BackendConfigType.BOOLEAN)
        write { prefs.edit().putBoolean(prefsKey(field), value).commit() }
    }

    /** 写入 INT 配置值（同步 commit；未声明/类型不符 ⇒ 拒绝）。 */
    override fun putInt(name: String, value: Int) {
        val field = typed(name, BackendConfigType.INT)
        write { prefs.edit().putInt(prefsKey(field), value).commit() }
    }

    /** 写入 LONG 配置值（同步 commit；未声明/类型不符 ⇒ 拒绝）。 */
    override fun putLong(name: String, value: Long) {
        val field = typed(name, BackendConfigType.LONG)
        write { prefs.edit().putLong(prefsKey(field), value).commit() }
    }

    /** 写入 STRING 配置值（同步 commit；长度受 schema 默认值上限约束）。 */
    override fun putString(name: String, value: String) {
        val field = typed(name, BackendConfigType.STRING)
        require(value.length <= MAX_STRING_LENGTH) { "backend config string too large: " + name }
        write { prefs.edit().putString(prefsKey(field), value).commit() }
    }

    /** 清除该 backend schema 声明的全部配置值（不影响其它 backend）。 */
    override fun clear() {
        val editor = prefs.edit()
        for (field in schema.keys) {
            editor.remove(prefsKey(field))
        }
        write { editor.commit() }
    }

    /**
     * @return 完整 prefs key（`<cve>:<name>`，供报告/日志核对，不用于读写）。
     */
    override fun fullKey(name: String): String = prefsKey(field(name))

    /** @return 声明的配置项；未声明 ⇒ fail-closed。 */
    private fun field(name: String): BackendConfigKey =
        schema.key(name)
            ?: throw BackendConfigViolation("undeclared backend config key: " + name)

    /** @return 声明项且类型匹配；否则 fail-closed。 */
    private fun typed(name: String, type: BackendConfigType): BackendConfigKey {
        val field = field(name)
        if (field.type != type) {
            throw BackendConfigViolation(
                "backend config type mismatch: " + name + " expected " + field.type + " got " + type,
            )
        }
        return field
    }

    /** 读取：缺失或损坏（ClassCastException）⇒ schema 默认（损坏项清除并记日志）。 */
    private inline fun <T> readOrDefault(field: BackendConfigKey, reader: () -> T): T {
        if (!prefs.contains(prefsKey(field))) {
            @Suppress("UNCHECKED_CAST")
            return field.defaultValue as T
        }
        return try {
            reader()
        } catch (e: ClassCastException) {
            Log.w(TAG, "corrupt config value removed: " + prefsKey(field), e)
            prefs.edit().remove(prefsKey(field)).commit()
            @Suppress("UNCHECKED_CAST")
            field.defaultValue as T
        }
    }

    /** 写入并校验 commit 结果；失败即抛异常（fail-closed，不静默丢配置）。 */
    private inline fun write(action: () -> Boolean) {
        if (!action()) {
            throw BackendConfigViolation("backend config commit failed")
        }
    }

    /** @return 实际 prefs key（命名空间由框架拼）。 */
    private fun prefsKey(field: BackendConfigKey): String = cve + ":" + field.name

    companion object {
        private const val TAG = "BackendConfigStore"

        /** STRING 值最大长度（禁大对象）。 */
        private const val MAX_STRING_LENGTH = 8 * 1024

        /** prefs 文件名前缀（每个 backend 独立文件）。 */
        private const val PREFS_PREFIX = "ghostlock.backend.config."

        /**
         * 打开一个 backend 的配置 store（data adapter 创建并持有）。
         *
         * @param context 应用上下文。
         * @param cve 所属 backend 的 CVE 编号。
         * @param schema 该 backend 声明的配置项。
         * @return store；命名空间固定为 `cve:name`。
         */
        fun open(context: Context, cve: String, schema: BackendConfigSchema): BackendConfigStore {
            val app = context.applicationContext
            val prefs = app.getSharedPreferences(PREFS_PREFIX + cve, Context.MODE_PRIVATE)
            return SharedPreferencesBackendConfigStore(prefs, app, cve, schema)
        }
    }
}
