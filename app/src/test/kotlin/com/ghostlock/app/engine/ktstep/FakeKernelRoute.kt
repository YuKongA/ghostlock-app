package com.ghostlock.app.engine.ktstep

/** 记录请求/页，按请求返回脚本化结果。 */
class FakeKernelRoute : KernelRoute {
    val requests = mutableListOf<WriteRequest>()
    val pages = mutableListOf<ByteArray>()
    var answer: (WriteRequest) -> RouteResult = { RouteResult.Landed() }

    override fun attempt(
        request: WriteRequest,
        page: ByteArray,
        inputs: PayloadPageInputs,
    ): RouteResult {
        requests += request
        pages += page.copyOf()
        return answer(request)
    }
}
