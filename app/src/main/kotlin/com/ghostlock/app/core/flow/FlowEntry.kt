package com.ghostlock.app.core.flow

import android.content.Context
import java.lang.reflect.InvocationTargetException
import java.lang.reflect.Method
import java.lang.reflect.Modifier

/**
 * 标记一个可被反射调用的 Flow 入口函数。
 *
 * 约束（计划 §2.4）：目标类必须实现 [Flow]；方法必须是 public、非 static、
 * 非 synthetic/bridge、非 varargs、恰好一个 [FlowHostEnv] 参数且返回类型**精确**
 * 为 [FlowResult]；`declaringClass` 必须等于目标类；只允许 Kotlin `object`
 * （`INSTANCE`）或可访问无参构造器实例化。
 *
 * @note `@Target`/`@Retention` 写死：只能标函数、运行期可见（registry 依赖）。
 * @note 阶段：B1 契约层。
 */
@Target(AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.RUNTIME)
annotation class FlowEntry

/**
 * host 进程提供给 Flow 入口的运行环境。
 *
 * @property params 已通过类型/预算校验的结构化参数。
 * @property payload 可选二进制 payload；null 表示未提供。
 * @note 实现方属于 B2 host；B1 只定义契约。
 */
interface FlowHostEnv {
    /**
     * host 进程本地 `Context`（B4b；**不跨 Binder、不写日志、不持久化**）。
     *
     * app `:flowhost` 传 Service context；Shizuku host 传 package context（可为 null）。
     * 入口按需读取；不得 close/invalidate host 的 Context。
     *
     * @note 默认 null：context-free 入口（如 43499-Glued）与 JVM 测试环境无需实现。
     */
    val context: Context?
        get() = null

    /** @return 结构化参数（不可变）。 */
    val params: FlowHostParams

    /** @return 可选 payload；null 表示未提供。 */
    val payload: ByteArray?

    /**
     * 写一行 host 日志。
     *
     * @param line 日志正文；不得包含密钥。
     */
    fun log(line: String)

    /** @return true 表示 host 已请求取消；入口应尽快返回。 */
    fun isCancelled(): Boolean
}

/** 一次 symbol 校验的具名结果。 */
sealed interface FlowEntryResolution {
    /** 校验通过：可调用入口。 */
    data class Resolved(val entry: FlowEntrySpec) : FlowEntryResolution

    /** 校验失败：`flow-symbol:*` 具名原因。 */
    data class Rejected(val reason: String) : FlowEntryResolution
}

/**
 * 已解析的 Flow 入口：绑定目标实例与反射方法。
 *
 * @property symbol 本入口的反射符号。
 * @note 构造为 internal：只允许 [FlowEntryValidator] 产出。
 * @note 阶段：B1 契约层。
 */
class FlowEntrySpec internal constructor(
    val symbol: FlowSymbol,
    private val method: Method,
    private val instance: Any,
) {
    /**
     * 调用入口方法。
     *
     * @param env host 运行环境。
     * @return 入口返回的 FlowResult（强类型，已在校验期保证精确类型）。
     * @throws InvocationTargetException 入口抛出的业务异常（不吞、不包装）。
     * @throws IllegalAccessException 反射访问失败（校验后理论不发生）。
     * @throws ClassCastException 返回值非 FlowResult（校验后理论不发生）。
     * @note 只调用登记方法；不额外实例化/不调用其它方法。
     */
    fun invoke(env: FlowHostEnv): FlowResult =
        method.invoke(instance, env) as FlowResult
}

/**
 * Flow 入口的反射校验器（只用 `java.lang.reflect`）。
 *
 * 校验顺序（计划 §2.4）：类加载 → [Flow] 可赋值 → `declaredMethods` 中方法名唯一
 * → 签名/修饰符 → 返回类型精确 [FlowResult] → [FlowEntry] 注解 → 实例化。
 * 任一失败返回 [FlowEntryResolution.Rejected]，**绝不调用**入口/构造之外的东西。
 *
 * @note 类加载使用 [FlowEntry] 的 classloader（不依赖线程上下文 classloader）。
 * @note 无新增可变全局；校验器本身无状态。
 * @note 阶段：B1 契约层。
 */
object FlowEntryValidator {
    /**
     * 校验一个反射符号。
     *
     * @param symbol 待校验符号。
     * @return [FlowEntryResolution.Resolved] 或具名 [FlowEntryResolution.Rejected]。
     */
    fun validate(symbol: FlowSymbol): FlowEntryResolution {
        val clazz = loadClass(symbol.className)
            ?: return rejected("flow-symbol:class-not-found")
        if (!Flow::class.java.isAssignableFrom(clazz)) {
            return rejected("flow-symbol:not-a-flow")
        }
        val declared = clazz.declaredMethods.filter { it.name == symbol.methodName }
        if (declared.isEmpty()) {
            val inherited = clazz.methods.firstOrNull { method ->
                method.name == symbol.methodName &&
                    method.parameterTypes.contentEquals(arrayOf(FlowHostEnv::class.java))
            }
            return rejected(
                if (inherited != null) {
                    "flow-symbol:declaring-class-mismatch"
                } else {
                    "flow-symbol:method-not-found"
                },
            )
        }
        if (declared.size > 1) return rejected("flow-symbol:overloaded")
        val method = declared.single()
        if (method.declaringClass != clazz) {
            return rejected("flow-symbol:declaring-class-mismatch")
        }
        if (method.isVarArgs) return rejected("flow-symbol:method-varargs")
        if (method.parameterTypes.size != 1 ||
            method.parameterTypes[0] != FlowHostEnv::class.java
        ) {
            return rejected("flow-symbol:wrong-signature")
        }
        if (!Modifier.isPublic(method.modifiers)) {
            return rejected("flow-symbol:method-not-public")
        }
        if (Modifier.isStatic(method.modifiers)) {
            return rejected("flow-symbol:method-static")
        }
        if (method.isSynthetic) return rejected("flow-symbol:method-synthetic")
        if (method.isBridge) return rejected("flow-symbol:method-bridge")
        if (method.typeParameters.isNotEmpty()) {
            return rejected("flow-symbol:wrong-signature")
        }
        if (method.returnType != FlowResult::class.java) {
            return rejected("flow-symbol:wrong-return")
        }
        if (!method.isAnnotationPresent(FlowEntry::class.java)) {
            return rejected("flow-symbol:not-annotated")
        }
        val instance = instantiate(clazz) ?: return rejected("flow-symbol:not-instantiable")
        return FlowEntryResolution.Resolved(FlowEntrySpec(symbol, method, instance))
    }

    /**
     * 加载目标类（不初始化静态块）。
     *
     * @param className Java 二进制名。
     * @return 类对象；加载失败返回 null。
     */
    private fun loadClass(className: String): Class<*>? = try {
        Class.forName(className, false, FlowEntry::class.java.classLoader)
    } catch (error: ClassNotFoundException) {
        null
    } catch (error: LinkageError) {
        null
    }

    /**
     * 实例化目标类：仅 Kotlin `object` 的 `INSTANCE` 或 public 无参构造器。
     *
     * @param clazz 目标类。
     * @return 实例；不可实例化返回 null。
     */
    private fun instantiate(clazz: Class<*>): Any? {
        val instanceField = runCatching {
            clazz.getDeclaredField("INSTANCE")
        }.getOrNull()
        if (instanceField != null &&
            Modifier.isStatic(instanceField.modifiers) &&
            clazz.isAssignableFrom(instanceField.type)
        ) {
            return runCatching { instanceField.get(null) }.getOrNull()
        }
        val constructor = runCatching { clazz.getDeclaredConstructor() }.getOrNull()
            ?: return null
        if (!Modifier.isPublic(constructor.modifiers)) return null
        return runCatching { constructor.newInstance() }.getOrNull()
    }

    /** @return 具名拒绝结果。 */
    private fun rejected(reason: String): FlowEntryResolution.Rejected =
        FlowEntryResolution.Rejected(reason)
}
