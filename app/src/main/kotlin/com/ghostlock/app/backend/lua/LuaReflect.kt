package com.ghostlock.app.backend.lua

import org.luaj.vm2.LuaError
import org.luaj.vm2.LuaValue
import org.luaj.vm2.Varargs
import org.luaj.vm2.lib.VarArgFunction
import org.luaj.vm2.lib.jse.CoerceJavaToLua

/**
 * Lua 的反射桥：
 *
 * reflect("java.lang.System", "getProperty", "java.version")
 * reflect(javaObject, "methodName", arg1, arg2)
 *
 * 第一个参数是字符串时调用静态方法；是 userdata 时调用该对象的实例方法。
 * 参数按目标方法的参数类型做最小转换。
 * 这是脚本能力，不是安全边界；Lua 脚本本来就被允许调用任意可反射函数。
 */
class LuaReflect : VarArgFunction() {
    override fun invoke(args: Varargs): Varargs {
        if (args.narg() < 2) {
            throw LuaError("reflect(className, method, ...args)")
        }
        val receiver = if (args.arg(1).isuserdata()) args.arg(1).touserdata() else null
        val className = if (receiver == null) args.arg(1).tojstring() else receiver.javaClass.name
        val methodName = args.arg(2).tojstring()
        val count = args.narg() - 2
        val type = if (receiver != null) receiver.javaClass else try {
            Class.forName(className)
        } catch (problem: Throwable) {
            throw LuaError("reflect: class not found: " + className)
        }
        val methods = type.methods.filter { it.name == methodName && it.parameterCount == count }
        if (methods.isEmpty()) {
            throw LuaError("reflect: method not found: " + className + "." + methodName + "/" + count)
        }
        var last: Throwable? = null
        for (method in methods) {
            try {
                val actual = arrayOfNulls<Any?>(count)
                for (index in 0 until count) {
                    actual[index] = convert(args.arg(index + 3), method.parameterTypes[index])
                }
                val result = method.invoke(receiver, *actual)
                return CoerceJavaToLua.coerce(result)
            } catch (problem: Throwable) {
                last = problem
            }
        }
        throw LuaError("reflect: invoke failed: " + className + "." + methodName + ": " + (last?.message ?: "unknown"))
    }

    companion object {
        internal fun convert(value: LuaValue, type: Class<*>): Any? {
            if (value.isnil()) return null
            return when {
                type == String::class.java || type == CharSequence::class.java -> value.tojstring()
                type == Int::class.javaPrimitiveType || type == Integer::class.java -> value.toint()
                type == Long::class.javaPrimitiveType || type == java.lang.Long::class.java -> value.tolong()
                type == Short::class.javaPrimitiveType || type == java.lang.Short::class.java -> value.toshort()
                type == Byte::class.javaPrimitiveType || type == java.lang.Byte::class.java -> value.tobyte()
                type == Double::class.javaPrimitiveType || type == java.lang.Double::class.java -> value.todouble()
                type == Float::class.javaPrimitiveType || type == java.lang.Float::class.java -> value.tofloat()
                type == Boolean::class.javaPrimitiveType || type == java.lang.Boolean::class.java -> value.toboolean()
                type == Char::class.javaPrimitiveType || type == java.lang.Character::class.java -> value.tochar()
                else -> value.touserdata() ?: value.tojstring()
            }
        }
    }
}
