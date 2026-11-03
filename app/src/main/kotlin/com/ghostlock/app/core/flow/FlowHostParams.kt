package com.ghostlock.app.core.flow

import android.os.Bundle
import java.util.Collections

/**
 * 一次 Flow run 的结构化参数（类型白名单 + 预算受限）。
 *
 * 类型白名单（计划 §2.3）：`String`、`Int`、`Long`、`Boolean`、`Float`、
 * `Double`、`ByteArray`、`List<String>`；禁止任意 `Parcelable`/`Binder`/嵌套
 * `Bundle`/`FileDescriptor`。超限/未知类型一律 fail-closed 并给具名 reason。
 *
 * @note 不可变：内部 map 在构造时复制并包成只读视图；无新增可变全局。
 * @note 阶段：B1 契约层（新进程攻击机制）；B2 才接 host/AIDL。
 */
class FlowHostParams private constructor(
    private val values: Map<String, Any?>,
) {
    /** @return 不可变参数视图（键顺序 = 插入顺序）。 */
    fun values(): Map<String, Any?> = values

    /** @param key 参数键。 @return String 值；缺失/类型不符返回 null。 */
    fun getString(key: String): String? = values[key] as? String

    /** @param key 参数键。 @return Int 值；缺失/类型不符返回 null。 */
    fun getInt(key: String): Int? = values[key] as? Int

    /** @param key 参数键。 @return Long 值；缺失/类型不符返回 null。 */
    fun getLong(key: String): Long? = values[key] as? Long

    /** @param key 参数键。 @return Boolean 值；缺失/类型不符返回 null。 */
    fun getBoolean(key: String): Boolean? = values[key] as? Boolean

    /** @param key 参数键。 @return Float 值；缺失/类型不符返回 null。 */
    fun getFloat(key: String): Float? = values[key] as? Float

    /** @param key 参数键。 @return Double 值；缺失/类型不符返回 null。 */
    fun getDouble(key: String): Double? = values[key] as? Double

    /** @param key 参数键。 @return ByteArray 的副本；缺失/类型不符返回 null。 */
    fun getByteArray(key: String): ByteArray? = (values[key] as? ByteArray)?.copyOf()

    /** @param key 参数键。 @return List<String> 副本；缺失/类型不符返回 null。 */
    fun getStringList(key: String): List<String>? =
        @Suppress("UNCHECKED_CAST")
        (values[key] as? List<String>)?.toList()

    /**
     * 校验参数键是否都在允许 schema 内。
     *
     * @param allowedKeys 允许的键集合（来自 BackendConfigStore schema）。
     * @throws IllegalArgumentException 存在未知键：`flow-params:unknown-key:<key>`。
     * @note 只做键集校验；类型/大小在构造时已校验。
     */
    fun validateSchema(allowedKeys: Set<String>) {
        val unknown = values.keys.filterNot { it in allowedKeys }.sorted()
        if (unknown.isNotEmpty()) {
            throw paramsFailure("flow-params:unknown-key:" + unknown.first())
        }
    }

    companion object {
        /** 参数序列化总预算（计划 §2.3：≤256 KiB）。 */
        internal const val MAX_TOTAL_BYTES: Int = 256 * 1024

        /** 参数键数上限（计划 §2.3：≤32）。 */
        internal const val MAX_KEYS: Int = 32

        /** 单个 String 的 UTF-8 字节上限（计划 §2.3：≤8 KiB）。 */
        internal const val MAX_STRING_BYTES: Int = 8 * 1024

        /**
         * 从 map 创建参数（唯一构造入口；codec 也经此校验）。
         *
         * @param values 待校验参数（按插入顺序保存副本）。
         * @return 校验通过的不可变参数对象。
         * @throws IllegalArgumentException 具名 `flow-params:*`（键数/类型/单串/总量）。
         * @note null 值不在白名单内（视为 unsupported-type），避免 Bundle 空值歧义。
         */
        fun of(values: Map<String, Any?>): FlowHostParams {
            if (values.size > MAX_KEYS) throw paramsFailure("flow-params:too-many-keys")
            val copy = LinkedHashMap<String, Any?>(values.size)
            var estimated = 0
            for ((key, value) in values) {
                if (value == null) throw paramsFailure("flow-params:unsupported-type:$key")
                estimated += utf8Length(key)
                estimated += when (value) {
                    is String -> {
                        val bytes = utf8Length(value)
                        if (bytes > MAX_STRING_BYTES) {
                            throw paramsFailure("flow-params:string-too-long:$key")
                        }
                        bytes
                    }
                    is ByteArray -> value.size
                    is List<*> -> {
                        var total = 0
                        for (element in value) {
                            if (element !is String) {
                                throw paramsFailure("flow-params:unsupported-type:$key")
                            }
                            val bytes = utf8Length(element)
                            if (bytes > MAX_STRING_BYTES) {
                                throw paramsFailure("flow-params:string-too-long:$key")
                            }
                            total += bytes
                        }
                        total
                    }
                    is Int -> 4
                    is Long -> 8
                    is Boolean -> 1
                    is Float -> 4
                    is Double -> 8
                    else -> throw paramsFailure("flow-params:unsupported-type:$key")
                }
                if (estimated > MAX_TOTAL_BYTES) throw paramsFailure("flow-params:too-large")
                copy[key] = value
            }
            return FlowHostParams(Collections.unmodifiableMap(copy))
        }

        /** @return 字符串的 UTF-8 字节长度（预算估算用）。 */
        private fun utf8Length(value: String): Int =
            value.toByteArray(Charsets.UTF_8).size
    }
}

/**
 * `FlowHostParams` / Bundle codec。
 *
 * @note 编码只写白名单类型；解码对非白名单类型/超预算 fail-closed（具名 reason），
 *   不静默丢弃、不回退默认值。
 * @note 不引入 Parcelable/Binder；Bundle 仅作为 AIDL 传输容器。
 * @note 阶段：B1 契约层。
 */
object FlowHostParamsCodec {
    /**
     * 把参数编码为 `android.os.Bundle`。
     *
     * @param params 已校验参数。
     * @return 只含白名单类型的 Bundle（新对象，可直接跨 Binder）。
     */
    fun encode(params: FlowHostParams): Bundle {
        val values = encodeValues(params)
        val bundle = Bundle()
        for ((key, value) in values) {
            when (value) {
                is String -> bundle.putString(key, value)
                is Int -> bundle.putInt(key, value)
                is Long -> bundle.putLong(key, value)
                is Boolean -> bundle.putBoolean(key, value)
                is Float -> bundle.putFloat(key, value)
                is Double -> bundle.putDouble(key, value)
                is ByteArray -> bundle.putByteArray(key, value)
                is List<*> -> bundle.putStringArrayList(key, ArrayList(value.map { it as String }))
                else -> throw IllegalArgumentException("flow-params:unsupported-type:$key")
            }
        }
        return bundle
    }

    /**
     * 值层编码（不依赖 Android；Bundle 层只是搬运）。
     *
     * @param params 已校验参数。
     * @return 值层 map（ArrayList 形态，模拟 Bundle 解包后的容器类型）。
     * @throws IllegalArgumentException 具名 `flow-params:*`。
     * @note B1 单测用该层验证 codec 逻辑；Bundle 真实 wire 属 B2/:flowhost。
     */
    internal fun encodeValues(params: FlowHostParams): Map<String, Any?> {
        val values = LinkedHashMap<String, Any?>()
        for ((key, value) in params.values()) {
            values[key] = when (value) {
                is List<*> -> ArrayList(value.map { it as String })
                else -> value
            }
        }
        return values
    }

    /**
     * 值层解码（不依赖 Android；Bundle 解包后的 map 交给本函数做白名单/预算校验）。
     *
     * @param values Bundle 解包得到的值 map（容器可能是 ArrayList）。
     * @return 校验通过的参数对象。
     * @throws IllegalArgumentException 具名 `flow-params:*`（非白名单类型/超预算）。
     * @note B1 单测用该层；Bundle 读取逻辑在 [decode] 中只做搬运。
     */
    internal fun decodeValues(values: Map<String, Any?>): FlowHostParams {
        val decoded = LinkedHashMap<String, Any?>()
        for ((key, raw) in values) {
            decoded[key] = when (raw) {
                is String, is Int, is Long, is Boolean, is Float, is Double -> raw
                is ByteArray -> raw.copyOf()
                is List<*> -> {
                    if (raw.all { it is String }) {
                        raw.map { it as String }.toList()
                    } else {
                        throw IllegalArgumentException("flow-params:unsupported-type:$key")
                    }
                }
                else -> throw IllegalArgumentException("flow-params:unsupported-type:$key")
            }
        }
        return FlowHostParams.of(decoded)
    }

    /**
     * 从 `Bundle` 解码参数。
     *
     * @param bundle host 收到的参数 Bundle。
     * @return 校验通过的参数对象。
     * @throws IllegalArgumentException 具名 `flow-params:*`（非白名单类型/超预算）。
     * @note 逐键读取；不信任 Bundle 的类加载器/类型，全部重新分类校验。
     */
    fun decode(bundle: Bundle): FlowHostParams {
        val values = LinkedHashMap<String, Any?>()
        for (key in bundle.keySet()) {
            values[key] = bundle.get(key)
        }
        return decodeValues(values)
    }
}

/** @return 统一的具名参数错误（IllegalArgumentException 子类）。 */
private fun paramsFailure(reason: String): IllegalArgumentException =
    IllegalArgumentException(reason)
