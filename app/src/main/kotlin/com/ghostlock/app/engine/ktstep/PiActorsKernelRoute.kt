package com.ghostlock.app.engine.ktstep

/**
 * 诊断用 KernelRoute：跑一次 Kotlin PI actors，但**不写页**，所以永远不回答 Landed。
 *
 * 用途是 K0b 的离线/真机探针：把线程编排和 futex 桥接进 KtStepEngine，同时用
 * `Indeterminate` 冻结引擎，避免把“requeue 成功”误当成 root。
 */
class PiActorsKernelRoute(
    private val syscalls: KernelSyscalls,
    private val config: PiActorsConfig = PiActorsConfig(requeueSettleMs = 50L),
) : KernelRoute {
    override val kind: RouteKind = RouteKind.Diagnostic

    override fun attempt(
        request: WriteRequest,
        page: ByteArray,
        inputs: PayloadPageInputs,
    ): RouteResult {
        val made = when (val result = KernelFutexWords.create(syscalls)) {
            is FutexWordsResult.Errno -> return RouteResult.Refused("mmap:" + result.code)
            is FutexWordsResult.Ok -> result.words
        }
        val actors = KotlinPiActors(made.chain, made.target, made.wait, config).run()
        val close = made.close()
        if (actors.timedOut) return RouteResult.Indeterminate("probe:timeout")
        if (close is SyscallResult.Errno) {
            return RouteResult.Indeterminate("probe:close:" + close.code)
        }
        if (actors.requeueResult >= 1) {
            return RouteResult.Indeterminate(
                "probe:requeue=" + actors.requeueResult + ":page-write-unimplemented",
            )
        }
        return RouteResult.Refused("probe:requeue=" + actors.requeueResult)
    }
}
