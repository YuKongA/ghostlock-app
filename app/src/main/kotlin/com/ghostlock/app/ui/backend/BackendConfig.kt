package com.ghostlock.app.ui.backend

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import androidx.compose.runtime.Immutable

/**
 * 一类 backend 配置值的类型（**仅四种**；禁密钥/PII/大对象）。
 *
 * @note 阶段：task-27 UI 持久化框架；类型由 UI 框架统一校验，backend 不得自造类型。
 */
enum class BackendConfigType {
    /** 布尔开关。 */
    BOOLEAN,

    /** 32 位整数（小范围枚举/计数）。 */
    INT,

    /** 64 位整数（时间戳/大计数）。 */
    LONG,

    /** 短字符串（≤ 8 KiB；禁密钥/令牌/PII/大对象）。 */
    STRING,
}

/**
 * backend 声明的一个持久化配置项。
 *
 * key 规则（由 [BackendConfigSchema] 与 `BackendConfigStore` 强制）：
 * - 必须是稳定的小写 snake_case（`[a-z][a-z0-9_]*`），≤ 64 字符，不含 `:`；
 * - 命名空间由 UI 框架拼成 `<cve>:<name>`，backend **不得**自带命名空间；
 * - 必须显式给出与 [type] 匹配的默认值（禁止 null 默认）。
 *
 * @property name snake_case 配置项名（不含命名空间）。
 * @property type 配置值类型。
 * @property defaultValue 显式默认值（类型必须与 [type] 一致）。
 * @note 阶段：task-27；配置项规则由 store 强制，违反即 fail-closed 抛 [BackendConfigViolation]。
 */
@Immutable
data class BackendConfigKey(
    val name: String,
    val type: BackendConfigType,
    val defaultValue: Any,
) {
    init {
        require(name.matches(NAME_REGEX)) {
            "backend config key must be snake_case: " + name
        }
        require(!name.contains(':')) {
            "backend config key must not carry a namespace: " + name
        }
        require(name.length <= MAX_NAME_LENGTH) {
            "backend config key too long: " + name
        }
        require(defaultValueMatches(type, defaultValue)) {
            "backend config default type mismatch: " + name
        }
        if (defaultValue is String) {
            require(defaultValue.length <= MAX_STRING_LENGTH) {
                "backend config string default too large: " + name
            }
        }
    }

    companion object {
        /** snake_case 名称正则。 */
        private val NAME_REGEX = Regex("[a-z][a-z0-9_]*")

        /** 单项 key 最大长度。 */
        private const val MAX_NAME_LENGTH = 64

        /** STRING 默认值最大长度（禁大对象）。 */
        private const val MAX_STRING_LENGTH = 8 * 1024

        /**
         * @return [defaultValue] 是否与 [type] 匹配。
         */
        private fun defaultValueMatches(type: BackendConfigType, defaultValue: Any): Boolean =
            when (type) {
                BackendConfigType.BOOLEAN -> defaultValue is Boolean
                BackendConfigType.INT -> defaultValue is Int
                BackendConfigType.LONG -> defaultValue is Long
                BackendConfigType.STRING -> defaultValue is String
            }
    }
}

/**
 * 一个 backend 的完整配置 schema。
 *
 * @property keys 声明的配置项（名称唯一、总数 ≤ 64）。
 * @note 阶段：task-27；空 schema 合法（表示该 backend 当前没有可持久化配置）。
 */
@Immutable
data class BackendConfigSchema(
    val keys: List<BackendConfigKey> = emptyList(),
) {
    init {
        require(keys.size <= MAX_KEYS) { "too many backend config keys" }
        require(keys.map { it.name }.toSet().size == keys.size) {
            "duplicate backend config key"
        }
    }

    /** @return 名为 [name] 的声明；未声明返回 null（调用方应 fail-closed）。 */
    fun key(name: String): BackendConfigKey? = keys.firstOrNull { it.name == name }

    companion object {
        /** 单项 backend 最多允许的配置项数。 */
        private const val MAX_KEYS = 64

        /** 空 schema（不持久化任何内容）。 */
        val EMPTY: BackendConfigSchema = BackendConfigSchema()
    }
}

/**
 * 配置项规则被违反（未声明/key 非法/类型不符）。
 *
 * @note fail-closed：store 直接抛本异常，由调用方 `runCatching` 记录并保持默认值；
 *   **不得**静默接受未声明配置。
 */
class BackendConfigViolation(message: String) : IllegalArgumentException(message)

/**
 * backend 配置的 UI 框架持久化 store（SharedPreferences 后端）。
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
 * - store 由 UI 框架创建/持有，backend 只能通过 `saveConfig/restoreConfig` 参数获得。
 *
 * @property context 应用上下文（backend restore 需要写回自己的运行时配置）。
 * @property cve 所属 backend 的 CVE 编号（命名空间）。
 * @property schema 该 backend 声明的配置项。
 * @note 阶段：task-27；写用 `commit()`（同步）以保证 force-stop 后重启可读到最新值。
 */
class BackendConfigStore internal constructor(
    private val prefs: SharedPreferences,
    val context: Context,
    val cve: String,
    val schema: BackendConfigSchema,
) {
    /** @return 声明项是否有已保存值。 */
    fun contains(name: String): Boolean = prefs.contains(prefsKey(field(name)))

    /** @return BOOLEAN 配置值（未保存时返回 schema 默认）。 */
    fun getBoolean(name: String): Boolean {
        val field = typed(name, BackendConfigType.BOOLEAN)
        return readOrDefault(field) {
            prefs.getBoolean(prefsKey(field), field.defaultValue as Boolean)
        }
    }

    /** @return INT 配置值（未保存时返回 schema 默认）。 */
    fun getInt(name: String): Int {
        val field = typed(name, BackendConfigType.INT)
        return readOrDefault(field) {
            prefs.getInt(prefsKey(field), field.defaultValue as Int)
        }
    }

    /** @return LONG 配置值（未保存时返回 schema 默认）。 */
    fun getLong(name: String): Long {
        val field = typed(name, BackendConfigType.LONG)
        return readOrDefault(field) {
            prefs.getLong(prefsKey(field), field.defaultValue as Long)
        }
    }

    /** @return STRING 配置值（未保存时返回 schema 默认；不含密钥/PII 由声明方保证）。 */
    fun getString(name: String): String {
        val field = typed(name, BackendConfigType.STRING)
        return readOrDefault(field) {
            prefs.getString(prefsKey(field), field.defaultValue as String) ?: field.defaultValue as String
        }
    }

    /** 写入 BOOLEAN 配置值（同步 commit；未声明/类型不符 ⇒ 拒绝）。 */
    fun putBoolean(name: String, value: Boolean) {
        val field = typed(name, BackendConfigType.BOOLEAN)
        write { prefs.edit().putBoolean(prefsKey(field), value).commit() }
    }

    /** 写入 INT 配置值（同步 commit；未声明/类型不符 ⇒ 拒绝）。 */
    fun putInt(name: String, value: Int) {
        val field = typed(name, BackendConfigType.INT)
        write { prefs.edit().putInt(prefsKey(field), value).commit() }
    }

    /** 写入 LONG 配置值（同步 commit；未声明/类型不符 ⇒ 拒绝）。 */
    fun putLong(name: String, value: Long) {
        val field = typed(name, BackendConfigType.LONG)
        write { prefs.edit().putLong(prefsKey(field), value).commit() }
    }

    /** 写入 STRING 配置值（同步 commit；长度受 schema 默认值上限约束）。 */
    fun putString(name: String, value: String) {
        val field = typed(name, BackendConfigType.STRING)
        require(value.length <= MAX_STRING_LENGTH) { "backend config string too large: " + name }
        write { prefs.edit().putString(prefsKey(field), value).commit() }
    }

    /** 清除该 backend schema 声明的全部配置值（不影响其它 backend）。 */
    fun clear() {
        val editor = prefs.edit()
        for (field in schema.keys) {
            editor.remove(prefsKey(field))
        }
        write { editor.commit() }
    }

    /**
     * @return 完整 prefs key（`<cve>:<name>`，供报告/日志核对，不用于读写）。
     */
    fun fullKey(name: String): String = prefsKey(field(name))

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
         * 打开一个 backend 的配置 store（UI 框架创建并持有）。
         *
         * @param context 应用上下文。
         * @param cve 所属 backend 的 CVE 编号。
         * @param schema 该 backend 声明的配置项。
         * @return store；命名空间固定为 `cve:name`。
         */
        fun open(context: Context, cve: String, schema: BackendConfigSchema): BackendConfigStore {
            val app = context.applicationContext
            val prefs = app.getSharedPreferences(PREFS_PREFIX + cve, Context.MODE_PRIVATE)
            return BackendConfigStore(prefs, app, cve, schema)
        }
    }
}
