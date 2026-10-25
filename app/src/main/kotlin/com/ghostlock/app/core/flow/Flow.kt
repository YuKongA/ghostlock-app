package com.ghostlock.app.core.flow

import kotlin.reflect.KClass

/**
 * 一个 backend 的完整执行入口。
 *
 * UI 只调用 run；每个 backend 自己组装步骤。Lua 等实现可以直接绕过 [SequentialFlow]。
 *
 * @param R 本 backend 自己的请求类型；必须是 [FlowRequest] 的具体子类，core 不提供默认实现。
 */
interface Flow<R : FlowRequest> {
    /** backend 身份；直接用实现类，避免 enum/wire string 成为第二套标识。 */
    val backendClass: KClass<out Flow<*>>

    /** 只接受本 backend 自己的请求类型；跨 backend 请求在编译期被拒绝。 */
    suspend fun run(request: R): FlowResult

    /** 取消/停止原语；无活动任务时必须是 no-op。 */
    fun stop()
}
