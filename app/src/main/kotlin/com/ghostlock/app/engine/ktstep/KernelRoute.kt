package com.ghostlock.app.engine.ktstep

/** 一次路由尝试的结局；由 Kotlin actor/batch 实现，策略仍在引擎侧。 */
enum class RouteKind { Production, Diagnostic, Fixture }

enum class ProbeKind { Production, Fixture }

/** 只有生产 route 实现此标记；fixture/诊断 route 不得实现。 */
interface ProductionRoute : KernelRoute

/** 只有生产 victim probe 实现此标记。 */
interface ProductionProbe : VictimProbe

sealed interface RouteResult {
    data class Landed(val attempts: Int = 1) : RouteResult
    data class Refused(val reason: String, val retryable: Boolean = false) : RouteResult
    data class Indeterminate(val reason: String) : RouteResult
}

/** 路由边界：输入是本步请求与已建好的页；实现不得改页/改请求。 */
fun interface KernelRoute {
    /** 默认 fixture；生产实现必须显式声明 Production。 */
    val kind: RouteKind get() = RouteKind.Fixture

    fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult
}
