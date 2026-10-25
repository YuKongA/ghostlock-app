package com.ghostlock.app.backend.lua

import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.log.log as appLog
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.luaj.vm2.LuaValue
import org.luaj.vm2.lib.OneArgFunction
import org.luaj.vm2.lib.jse.CoerceJavaToLua
import org.luaj.vm2.lib.jse.JsePlatform
import kotlin.coroutines.coroutineContext

/**
 * Lua 的 Flow 实现。
 *
 * run 直接启动脚本；步骤完全由脚本自己决定，不经过 SequentialFlow。
 * 脚本来源：request.payload 或 request.values["script"]。
 */
class FlowLuaImpl : Flow<FlowLuaRequest> {
    override val backendClass = FlowLuaImpl::class

    override suspend fun run(request: FlowLuaRequest): FlowResult {
        val script = request.payload?.toString(Charsets.UTF_8)
            ?: request.values["script"] as? String
        if (script.isNullOrBlank()) {
            return FlowResult.Failed(
                reason = "lua-script-missing",
                failedStep = null,
                steps = emptyList(),
                evidence = emptyMap(),
            )
        }
        return withContext(Dispatchers.IO) {
            coroutineContext.ensureActive()
            try {
                val globals = JsePlatform.standardGlobals()
                globals.set("log", object : OneArgFunction() {
                    override fun call(arg: LuaValue): LuaValue {
                        appLog.info(arg.tojstring())
                        return LuaValue.NONE
                    }
                })
                globals.set("reflect", LuaReflect())
                globals.set("new", LuaNew())
                globals.set("request", CoerceJavaToLua.coerce(request))
                val chunk = globals.load(script, "ghostlock-lua")
                val result = chunk.call()
                coroutineContext.ensureActive()
                FlowResult.Ok(
                    steps = emptyList(),
                    evidence = mapOf("lua.result" to result.tojstring()),
                )
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                FlowResult.Failed(
                    reason = "lua-error:" + problem::class.java.simpleName + ":" + (problem.message ?: ""),
                    failedStep = null,
                    steps = emptyList(),
                    evidence = emptyMap(),
                )
            }
        }
    }

    override fun stop() {
        // LuaJ runs on the caller's IO dispatcher; coroutine cancellation is the stop path.
    }
}
