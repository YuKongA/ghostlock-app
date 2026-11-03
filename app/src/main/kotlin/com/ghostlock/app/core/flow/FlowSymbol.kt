package com.ghostlock.app.core.flow

/**
 * 一个 Flow 入口的反射符号：`<fqcn>#<methodName>`。
 *
 * 目的：把「允许执行的 Flow 函数」表示成可持久化、可校验的纯文本，避免把
 * `Method`/`KClass` 等类加载期对象带入持久化/跨进程边界。
 *
 * @property className 目标类的 Java 二进制名（内部类用 `$` 分隔，例如
 *   `com.ghostlock.app.core.flow.FlowEntryValidationTest$ValidFlow`）。
 * @property methodName 目标方法名（Kotlin/Java 标识符，不含描述符）。
 * @note 构造时做标识符校验；非法值抛 [IllegalArgumentException]，不静默修正。
 * @note 阶段：B1 契约层（新进程攻击机制）；本类只做格式契约，不做反射/实例化。
 */
data class FlowSymbol(
    val className: String,
    val methodName: String,
) {
    init {
        require(CLASS_NAME_REGEX.matches(className)) {
            "flow-symbol:invalid-class:$className"
        }
        require(METHOD_NAME_REGEX.matches(methodName)) {
            "flow-symbol:invalid-method:$methodName"
        }
    }

    /** @return 规范形式 `<fqcn>#<methodName>`；解析/序列化唯一文本形态。 */
    val canonical: String
        get() = className + "#" + methodName

    private companion object {
        /** Java 标识符的单段；`$` 用于 Kotlin 内部类名。 */
        private const val SEGMENT = "[A-Za-z_$][A-Za-z0-9_$]*"

        /** 类名：以点分隔的合法段序列（允许内部类 `$`）。 */
        private val CLASS_NAME_REGEX = Regex("^$SEGMENT(?:\\$$SEGMENT)*(?:\\x2e$SEGMENT)*$")

        /** 方法名：单段合法标识符。 */
        private val METHOD_NAME_REGEX = Regex("^$SEGMENT$")
    }
}

/**
 * `FlowSymbol` 的严格文本解析器。
 *
 * @note 只接受 `^[A-Za-z_$][A-Za-z0-9_.$]*#[A-Za-z_$][A-Za-z0-9_$]*$`；空白、
 *   多个 `#`、空段、中文/控制字符、注释等一律返回 null（不抛异常、不修正）。
 * @note 阶段：B1 契约层。
 */
object FlowSymbols {
    /** 完整符号正则（计划 §2.4 的硬约束）。 */
    private val SYMBOL_REGEX =
        Regex("^[A-Za-z_$][A-Za-z0-9_.$]*#[A-Za-z_$][A-Za-z0-9_$]*$")

    /**
     * 解析文本符号。
     *
     * @param raw 原始文本；必须恰好包含一个 `#` 且两侧均为合法标识符。
     * @return 合法时返回 [FlowSymbol]；非法返回 null。
     */
    fun parse(raw: String): FlowSymbol? {
        if (!SYMBOL_REGEX.matches(raw)) return null
        val parts = raw.split('#')
        if (parts.size != 2) return null
        return runCatching {
            FlowSymbol(className = parts[0], methodName = parts[1])
        }.getOrNull()
    }
}
