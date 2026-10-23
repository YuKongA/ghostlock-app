package com.ghostlock.app.core.flow

/**
 * 一个 backend 的完整执行入口。
 *
 * UI 只调用 run；每个 backend 自己组装步骤。Lua 等实现可以直接绕过 [SequentialFlow]。
 */
interface Flow {
    /** backend 身份；直接用实现类，避免 enum/wire string 成为第二套标识。 */
    val backendClass: kotlin.reflect.KClass<out Flow>

    suspend fun run(request: FlowRequest): FlowResult

    /** 取消/停止原语；无活动任务时必须是 no-op。 */
    fun stop()
}
