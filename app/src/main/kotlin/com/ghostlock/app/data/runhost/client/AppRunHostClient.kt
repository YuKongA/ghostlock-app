package com.ghostlock.app.data.runhost.client

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.data.runhost.wire.toModel
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshot
import com.ghostlock.app.runhost.AppRunHostService
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull

/**
 * App-domain client for the same typed IRunHost contract used by Shizuku.
 *
 * @param context Application context used only for binding the dedicated `:flowhost` service.
 */
class AppRunHostClient(context: Context) {
    private val applicationContext = context.applicationContext
    private val lock = Any()
    private var currentHost: IRunHost? = null
    private var currentIdentity: RunHostIdentity? = null

    /**
     * Binds one fresh RunHost instance and waits for its immutable terminal snapshot.
     *
     * @param request Complete typed request.
     * @param logs Connector that independently drains and validates the RunHost callback source.
     * @return Terminal, stable rejection, or host-death outcome.
     */
    internal suspend fun run(
        request: RunHostRequest,
        logs: RunHostLogConnector,
    ): RunHostClientOutcome = suspendCancellableCoroutine { continuation ->
        val identity = runCatching { RunHostIdentity(request.runId, request.generation) }
            .getOrElse {
                logs.finishIncomplete("app-runhost-invalid-request")
                continuation.resume(RunHostClientOutcome.Rejected(RunHostCode.INVALID_REQUEST))
                return@suspendCancellableCoroutine
            }
        val finishing = AtomicBoolean(false)
        val completed = AtomicBoolean(false)
        val completionScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        var snapshotJob: Job? = null
        lateinit var connection: ServiceConnection

        fun finish(outcome: RunHostClientOutcome, host: IRunHost?) {
            if (!finishing.compareAndSet(false, true)) return
            snapshotJob?.cancel()
            completionScope.launch {
                if (outcome is RunHostClientOutcome.Terminal) {
                    val deliveryFinished = withTimeoutOrNull(RUNHOST_LOG_DELIVERY_TIMEOUT_MS) {
                        logs.terminalMarker.await()
                        logs.end.await()
                        true
                    } == true
                    if (!deliveryFinished) {
                        logs.finishIncomplete("app-runhost-terminal-marker-timeout")
                        logs.end.await()
                    }
                } else {
                    logs.finishIncomplete("app-runhost-" + outcome.logFailureName())
                    logs.end.await()
                }
                if (host != null &&
                    (outcome !is RunHostClientOutcome.TransportFailure || outcome.code != RunHostCode.HOST_DIED)
                ) {
                    runCatching { host.requestClose(identity.runId, identity.generation) }
                }
                synchronized(lock) {
                    if (currentHost === host) {
                        currentHost = null
                        currentIdentity = null
                    }
                }
                runCatching { applicationContext.unbindService(connection) }
                completed.set(true)
                if (continuation.isActive) continuation.resume(outcome)
            }
        }

        val callback = object : IRunHostCallback.Stub() {
            override fun onLog(record: RunHostLogRecord) {
                if (!completed.get()) logs.receive(record)
            }

            override fun onTerminal(snapshot: RunHostSnapshot) {
                val outcome = runCatching {
                    val decoded = snapshot.toModel()
                    require(decoded.identity == identity && decoded.state == RunHostState.TERMINAL)
                    logs.receiveTerminal(decoded)
                    RunHostClientOutcome.Terminal(decoded)
                }.getOrElse {
                    logs.finishIncomplete("app-runhost-terminal-invalid")
                    RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE)
                }
                finish(outcome, synchronized(lock) { currentHost })
            }
        }

        connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
                val host = IRunHost.Stub.asInterface(service)
                if (host == null) {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
                    return
                }
                if (!continuation.isActive) {
                    finish(RunHostClientOutcome.Rejected(RunHostCode.BAD_STATE), host)
                    return
                }
                synchronized(lock) {
                    currentHost = host
                    currentIdentity = identity
                }
                val callbackCode = runCatching {
                    RunHostCode.fromWireValue(host.setCallback(identity.runId, identity.generation, callback))
                }.getOrElse {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), host)
                    return
                }
                if (callbackCode != RunHostCode.OK) {
                    finish(RunHostClientOutcome.Rejected(callbackCode), host)
                    return
                }
                val prepareCode = runCatching {
                    RunHostCode.fromWireValue(host.prepare(request))
                }.getOrElse {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), host)
                    return
                }
                if (prepareCode != RunHostCode.OK && prepareCode != RunHostCode.ALREADY_APPLIED) {
                    finish(RunHostClientOutcome.Rejected(prepareCode), host)
                    return
                }
                val startCode = runCatching {
                    RunHostCode.fromWireValue(host.start(identity.runId, identity.generation))
                }.getOrElse {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), host)
                    return
                }
                if (startCode != RunHostCode.OK && startCode != RunHostCode.ALREADY_APPLIED) {
                    finish(RunHostClientOutcome.Rejected(startCode), host)
                    return
                }
                snapshotJob = CoroutineScope(SupervisorJob() + Dispatchers.IO).launch {
                    pollRunHostTerminal(host, identity, completed) { outcome -> finish(outcome, host) }
                }
            }

            override fun onServiceDisconnected(name: ComponentName?) {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
            }

            override fun onBindingDied(name: ComponentName?) {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
            }

            override fun onNullBinding(name: ComponentName?) {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
            }
        }

        continuation.invokeOnCancellation {
            logs.finishIncomplete("app-runhost-client-cancelled")
            val host = synchronized(lock) { currentHost }
            val active = synchronized(lock) { currentIdentity }
            if (host != null && active != null) {
                runCatching { host.cancel(active.runId, active.generation) }
            }
        }
        val bound = applicationContext.bindService(
            Intent(applicationContext, AppRunHostService::class.java),
            connection,
            Context.BIND_AUTO_CREATE,
        )
        if (!bound) finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
    }

    /** Requests typed cancellation without directly killing the service process. */
    fun cancelCurrent(): RunHostCode {
        val pair = synchronized(lock) { currentHost to currentIdentity }
        val host = pair.first ?: return RunHostCode.BAD_STATE
        val identity = pair.second ?: return RunHostCode.BAD_STATE
        return runCatching {
            RunHostCode.fromWireValue(host.cancel(identity.runId, identity.generation))
        }.getOrDefault(RunHostCode.HOST_DIED)
    }
}

/** Stable connector-only diagnostic for client outcomes; it never enters typed product facts. */
private fun RunHostClientOutcome.logFailureName(): String = when (this) {
    is RunHostClientOutcome.Terminal -> "terminal"
    is RunHostClientOutcome.Rejected -> "rejected-" + code.name.lowercase()
    is RunHostClientOutcome.TransportFailure -> "transport-" + code.name.lowercase()
}
