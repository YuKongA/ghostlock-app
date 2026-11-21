package com.ghostlock.app.data.runhost.client

import com.ghostlock.app.core.run.RunDispatchFailureStage
import com.ghostlock.app.core.run.RunDispatchLease
import com.ghostlock.app.core.run.RunDispatchPort
import com.ghostlock.app.core.run.RunDispatchPreparation
import com.ghostlock.app.core.run.RunInputCutoff
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.data.run.AndroidRunInputFreezer
import com.ghostlock.app.data.runhost.wire.toWireRequest
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.run.LogConnector
import com.ghostlock.app.domain.run.LogConnectorEnd
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.run.RunHostRequest
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope

/**
 * Android owner of input freezing, AIDL request assembly, and fixed-target client dispatch.
 *
 * @param freezer Sole same-open Android input freezer.
 * @param appClient Typed app-process RunHost client.
 * @param shizukuClient Optional typed Shizuku client used only when admission fixed that target.
 */
class AndroidRunHostDispatchAdapter(
    private val freezer: AndroidRunInputFreezer,
    private val appClient: AppRunHostClient,
    private val shizukuClient: ShizukuRunHostClient?,
) : RunDispatchPort {
    override val shizukuTypedHostAvailable: Boolean
        get() = shizukuClient != null

    /** Freezes one input, duplicates every request descriptor, and closes all master sources. */
    override suspend fun prepare(
        spec: BackendDescriptor,
        target: AttackExecutorKind,
        cutoff: RunInputCutoff,
    ): RunDispatchPreparation {
        val inputLease = try {
            freezer.freeze(spec, cutoff)
        } catch (problem: Throwable) {
            return RunDispatchPreparation.Failed(
                RunDispatchFailureStage.FREEZE,
                problem::class.java.simpleName,
            )
        }
        val request = try {
            inputLease.input.toWireRequest(inputLease::duplicate)
        } catch (problem: Throwable) {
            inputLease.close()
            return RunDispatchPreparation.Failed(
                RunDispatchFailureStage.REQUEST_ASSEMBLY,
                problem::class.java.simpleName,
            )
        }
        inputLease.close()
        return try {
            RunDispatchPreparation.Prepared(
                AndroidRunDispatchLease(request, target, appClient, shizukuClient),
            )
        } catch (problem: Throwable) {
            request.artifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
            RunDispatchPreparation.Failed(
                RunDispatchFailureStage.REQUEST_ASSEMBLY,
                problem::class.java.simpleName,
            )
        }
    }

    /** Single owner of the duplicated request descriptors after master-source closure. */
    private class AndroidRunDispatchLease(
        private val request: RunHostRequest,
        private val target: AttackExecutorKind,
        private val appClient: AppRunHostClient,
        private val shizukuClient: ShizukuRunHostClient?,
    ) : RunDispatchLease {
        private val closed = AtomicBoolean(false)

        override val identity: RunHostIdentity = RunHostIdentity(request.runId, request.generation)
        override val backendKey: String = request.backendKey

        /** Runs only the executor captured during admission and drains its callback connector. */
        override suspend fun dispatch(
            attach: suspend (LogConnector) -> LogConnectorEnd,
        ): RunExecutionResult = coroutineScope {
            check(!closed.get()) { "run dispatch lease is closed" }
            val connector = RunHostLogConnector(identity)
            val collection = async { attach(connector) }
            val outcome = try {
                when (target) {
                    AttackExecutorKind.APP -> appClient.run(request, connector)
                    AttackExecutorKind.SHIZUKU -> {
                        val client = shizukuClient
                            ?: return@coroutineScope RunExecutionResult.Refused(
                                listOf("The typed Shizuku RunHost is not connected."),
                            )
                        client.run(request, connector)
                    }
                }
            } finally {
                connector.finishIncomplete("runhost-client-returned-without-terminal-marker")
            }
            collection.await()
            outcome.toDomain()
        }

        /** Closes request duplicates once; client request ownership semantics remain unchanged. */
        override fun close() {
            if (!closed.compareAndSet(false, true)) return
            request.artifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
        }

        /** Maps typed transport outcomes without reading callback text. */
        private fun RunHostClientOutcome.toDomain(): RunExecutionResult = when (this) {
            is RunHostClientOutcome.Terminal -> RunExecutionResult.Completed(snapshot.toRunReport())
            is RunHostClientOutcome.Rejected ->
                RunExecutionResult.Refused(listOf("RunHost rejected: " + code.name))
            is RunHostClientOutcome.TransportFailure -> RunExecutionResult.TransportFailure(
                "RunHost transport failure: " + code.name,
            )
        }
    }
}
