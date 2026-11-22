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
import com.ghostlock.app.runhost.appuid.AppUidRunHostService
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull

/** App-UID client that accepts a result only after the one-shot `:flowhost` binder has died. */
class AppRunHostClient internal constructor(
    context: Context,
    private val testLatches: RunHostClientTestLatches,
) {
    constructor(context: Context) : this(context, RunHostClientTestLatches())

    private val applicationContext = context.applicationContext
    private val lock = Any()
    private var currentHost: IRunHost? = null
    private var currentIdentity: RunHostIdentity? = null
    private var quarantinedBinder: IBinder? = null

    /** Binds a fresh host, drains terminal logs, closes/destroys it, and observes physical binder death. */
    internal suspend fun run(
        request: RunHostRequest,
        logs: RunHostLogConnector,
    ): RunHostClientOutcome {
        if (quarantineBlocksRun()) {
            logs.finishIncomplete("app-runhost-process-quarantined")
            return RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE)
        }
        return suspendCancellableCoroutine { continuation ->
            val identity = runCatching { RunHostIdentity(request.runId, request.generation) }
                .getOrElse {
                    logs.finishIncomplete("app-runhost-invalid-request")
                    continuation.resume(RunHostClientOutcome.Rejected(RunHostCode.INVALID_REQUEST))
                    return@suspendCancellableCoroutine
                }
            val finishing = AtomicBoolean(false)
            val completed = AtomicBoolean(false)
            val expectedClosing = AtomicBoolean(false)
            val unbound = AtomicBoolean(false)
            val deathObserved = CompletableDeferred<Unit>()
            val completionScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
            var snapshotJob: Job? = null
            var linkedBinder: IBinder? = null
            lateinit var connection: ServiceConnection

            fun unbindOnce() {
                if (unbound.compareAndSet(false, true)) {
                    runCatching { applicationContext.unbindService(connection) }
                }
            }

            fun clearCurrent(host: IRunHost?) {
                synchronized(lock) {
                    if (currentHost === host) {
                        currentHost = null
                        currentIdentity = null
                    }
                }
            }

            fun finish(initial: RunHostClientOutcome, host: IRunHost?) {
                if (!finishing.compareAndSet(false, true)) return
                snapshotJob?.cancel()
                completionScope.launch {
                    var outcome = initial
                    if (initial is RunHostClientOutcome.Terminal) {
                        val drained = withTimeoutOrNull(RUNHOST_LOG_DELIVERY_TIMEOUT_MS) {
                            logs.terminalMarker.await()
                            logs.end.await()
                            true
                        } == true
                        if (!drained) {
                            logs.finishIncomplete("app-runhost-terminal-marker-timeout")
                            logs.end.await()
                        }
                    } else {
                        logs.finishIncomplete("app-runhost-" + initial.logFailureName())
                        logs.end.await()
                    }

                    val hostAlreadyDied = initial is RunHostClientOutcome.TransportFailure &&
                        initial.code == RunHostCode.HOST_DIED
                    if (host != null && !hostAlreadyDied) {
                        runCatching(testLatches.afterTerminalDrainBeforeClose)
                            .onFailure {
                                outcome = RunHostClientOutcome.TransportFailure(
                                    RunHostCode.INTERNAL_CONTRACT_FAILURE,
                                )
                            }
                        val closeCode = runCatching {
                            RunHostCode.fromWireValue(host.requestClose(identity.runId, identity.generation))
                        }.getOrDefault(RunHostCode.HOST_DIED)
                        if (closeCode != RunHostCode.OK && closeCode != RunHostCode.ALREADY_APPLIED) {
                            outcome = RunHostClientOutcome.TransportFailure(
                                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                            )
                        }
                        expectedClosing.set(true)
                        if (runCatching { host.destroy() }.isFailure) {
                            outcome = RunHostClientOutcome.TransportFailure(
                                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                            )
                        }
                        unbindOnce()
                        val died = withTimeoutOrNull(RUNHOST_PROCESS_DEATH_TIMEOUT_MS) {
                            deathObserved.await()
                            true
                        } == true
                        if (!died) {
                            linkedBinder?.let(::quarantine)
                            outcome = RunHostClientOutcome.TransportFailure(
                                RunHostCode.INTERNAL_CONTRACT_FAILURE,
                            )
                        }
                    } else {
                        unbindOnce()
                    }

                    clearCurrent(host)
                    completed.set(true)
                    if (continuation.isActive) continuation.resume(outcome)
                }
            }

            connection = object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
                    val host = service?.let(IRunHost.Stub::asInterface)
                    if (host == null) {
                        finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
                        return
                    }
                    val binder = host.asBinder()
                    linkedBinder = binder
                    val recipient = IBinder.DeathRecipient {
                        clearQuarantine(binder)
                        deathObserved.complete(Unit)
                        if (!expectedClosing.get()) {
                            finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), host)
                        }
                    }
                    if (runCatching { binder.linkToDeath(recipient, 0) }.isFailure) {
                        finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), host)
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
                    if (runCatching(testLatches.afterBindBeforeCallback).isFailure) {
                        finish(
                            RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE),
                            host,
                        )
                        return
                    }
                    if (finishing.get()) return
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
                                RunHostClientOutcome.TransportFailure(
                                    RunHostCode.INTERNAL_CONTRACT_FAILURE,
                                )
                            }
                            finish(outcome, host)
                        }
                    }
                    val callbackCode = runCatching {
                        RunHostCode.fromWireValue(
                            host.setCallback(identity.runId, identity.generation, callback),
                        )
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
                    if (!expectedClosing.get()) {
                        finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), currentHost)
                    }
                }

                override fun onBindingDied(name: ComponentName?) = onServiceDisconnected(name)

                override fun onNullBinding(name: ComponentName?) {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
                }
            }

            continuation.invokeOnCancellation {
                logs.finishIncomplete("app-runhost-client-cancelled")
                val pair = synchronized(lock) { currentHost to currentIdentity }
                if (pair.first != null && pair.second != null) {
                    runCatching { pair.first?.cancel(pair.second!!.runId, pair.second!!.generation) }
                    finish(
                        RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE),
                        pair.first,
                    )
                } else {
                    unbindOnce()
                }
            }
            val bound = applicationContext.bindService(
                Intent(applicationContext, AppUidRunHostService::class.java),
                connection,
                Context.BIND_AUTO_CREATE,
            )
            if (!bound) {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED), null)
            }
        }
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

    private fun quarantineBlocksRun(): Boolean = synchronized(lock) {
        val binder = quarantinedBinder ?: return@synchronized false
        if (binder.isBinderAlive) {
            true
        } else {
            quarantinedBinder = null
            false
        }
    }

    private fun quarantine(binder: IBinder) = synchronized(lock) {
        if (binder.isBinderAlive) quarantinedBinder = binder
    }

    private fun clearQuarantine(binder: IBinder) = synchronized(lock) {
        if (quarantinedBinder === binder) quarantinedBinder = null
    }
}

/** Stable connector-only diagnostic for client outcomes; it never enters typed product facts. */
private fun RunHostClientOutcome.logFailureName(): String = when (this) {
    is RunHostClientOutcome.Terminal -> "terminal"
    is RunHostClientOutcome.Rejected -> "rejected-" + code.name.lowercase()
    is RunHostClientOutcome.TransportFailure -> "transport-" + code.name.lowercase()
}
