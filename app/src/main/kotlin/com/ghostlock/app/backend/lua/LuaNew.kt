package com.ghostlock.app.backend.lua

import org.luaj.vm2.LuaError
import org.luaj.vm2.Varargs
import org.luaj.vm2.lib.VarArgFunction
import org.luaj.vm2.lib.jse.CoerceJavaToLua

/**
 * Lua 构造对象：
 *
 * new("java.lang.StringBuilder", "hello")
 * new("java.util.ArrayList")
 *
 * 返回 Java userdata，之后可用 reflect(obj, "method", ...) 调实例方法。
 */
class LuaNew : VarArgFunction() {
    override fun invoke(args: Varargs): Varargs {
        if (args.narg() < 1) {
            throw LuaError("new(className, ...args)")
        }
        val className = args.arg(1).tojstring()
        val type = try {
            Class.forName(className)
        } catch (problem: Throwable) {
            throw LuaError("new: class not found: " + className)
        }
        val count = args.narg() - 1
        val constructors = type.constructors.filter { it.parameterCount == count }
        if (constructors.isEmpty()) {
            throw LuaError("new: constructor not found: " + className + "/" + count)
        }
        var last: Throwable? = null
        for (constructor in constructors) {
            try {
                val actual = arrayOfNulls<Any?>(count)
                for (index in 0 until count) {
                    actual[index] = LuaReflect.convert(
                        args.arg(index + 2),
                        constructor.parameterTypes[index],
                    )
                }
                return CoerceJavaToLua.coerce(constructor.newInstance(*actual))
            } catch (problem: Throwable) {
                last = problem
            }
        }
        throw LuaError("new: construct failed: " + className + ": " + (last?.message ?: "unknown"))
    }
}
