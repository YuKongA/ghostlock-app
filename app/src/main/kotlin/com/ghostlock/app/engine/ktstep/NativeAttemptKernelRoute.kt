package com.ghostlock.app.engine.ktstep

/**
 * Production-shaped 43499 route: the C++ actor batch performs the page send and actor sequence.
 * Kotlin owns all criteria: only a complete page + started actors + requeue>=1 + wait==0 is Landed.
 */
class NativeAttemptKernelRoute(
    private val port: NativeAttemptPort,
    private val template: NativeAttemptParams,
) : ProductionRoute {
    override val kind: RouteKind = RouteKind.Production

    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult {
        /* Actor target/address stay template-owned; request.target describes the page write, not the futex word. */
        val params = template.copy(page = page)
        return when (val result = port.attempt(params)) {
            is NativeAttemptResult.Malformed -> RouteResult.Refused("attempt-malformed:" + result.reason)
            is NativeAttemptResult.Errno -> RouteResult.Refused("attempt:" + result.code)
            is NativeAttemptResult.Ok -> when {
                result.joinTimedOut -> RouteResult.Indeterminate("actors:join-timeout")
                result.pageErrno != 0 -> RouteResult.Refused("page:" + result.pageErrno)
                result.pageBytesWritten != page.size ->
                    RouteResult.Refused("page:" + result.pageBytesWritten + "/" + page.size)
                !result.actorsStarted -> RouteResult.Indeterminate("actors:not-started")
                result.requeueResult < 1 -> RouteResult.Indeterminate("actors:requeue:" + result.requeueResult)
                result.waitResult != 0 -> RouteResult.Indeterminate("actors:wait:" + result.waitResult)
                else -> RouteResult.Landed()
            }
        }
    }
}
