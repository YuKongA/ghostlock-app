package com.ghostlock.app.data.runhost.client

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.data.runhost.wire.toModel
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshot
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import rikka.shizuku.Shizuku

/**
 * Shizuku-domain typed client.
 *
 * @param binding Shizuku lifecycle adapter kept outside the typed business protocol.
 */
class ShizukuRunHostClient internal constructor(
    private val binding: ShizukuRunHostBinding,
) {
    /** Creates the production Shizuku binding adapter for one application process. */
    constructor(context: Context) : this(AndroidShizukuRunHostBinding(context))

    /** Runs one request through the shared typed wire. */
    internal suspend fun run(
        request: RunHostRequest,
        logs: RunHostLogConnector,
    ): RunHostClientOutcome {
        val identity = runCatching { RunHostIdentity(request.runId, request.generation) }
            .getOrElse {
                logs.finishIncomplete("shizuku-runhost-invalid-request")
                return RunHostClientOutcome.Rejected(RunHostCode.INVALID_REQUEST)
            }
        val host = runCatching { binding.bind() }
            .getOrElse {
                logs.finishIncomplete("shizuku-runhost-bind-failed")
                return RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED)
            }
        return suspendCancellableCoroutine { continuation ->
            val finishing = AtomicBoolean(false)
            val completed = AtomicBoolean(false)
            val completionScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
            var snapshotJob: Job? = null
            var deathRecipient: IBinder.DeathRecipient? = null
            fun finish(outcome: RunHostClientOutcome) {
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
                            logs.finishIncomplete("shizuku-runhost-terminal-marker-timeout")
                            logs.end.await()
                        }
                    } else {
                        logs.finishIncomplete("shizuku-runhost-" + outcome.shizukuLogFailureName())
                        logs.end.await()
                    }
                    deathRecipient?.let { recipient ->
                        runCatching { host.asBinder().unlinkToDeath(recipient, 0) }
                    }
                    if (outcome !is RunHostClientOutcome.TransportFailure ||
                        outcome.code != RunHostCode.HOST_DIED
                    ) {
                        val closeCode = runCatching {
                            RunHostCode.fromWireValue(host.requestClose(identity.runId, identity.generation))
                        }.getOrDefault(RunHostCode.HOST_DIED)
                        if (closeCode == RunHostCode.OK || closeCode == RunHostCode.ALREADY_APPLIED) {
                            runCatching { binding.close(host) }
                        }
                    } else {
                        runCatching { binding.close(host) }
                    }
                    completed.set(true)
                    if (continuation.isActive) continuation.resume(outcome)
                }
            }
            deathRecipient = IBinder.DeathRecipient {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED))
            }
            runCatching { host.asBinder().linkToDeath(requireNotNull(deathRecipient), 0) }
                .onFailure {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED))
                    return@suspendCancellableCoroutine
                }
            val callback = object : IRunHostCallback.Stub() {
                override fun onLog(record: RunHostLogRecord) {
                    if (!completed.get()) logs.receive(record)
                }

                override fun onTerminal(snapshot: RunHostSnapshot) {
                    finish(
                        runCatching {
                            val decoded = snapshot.toModel()
                            require(decoded.identity == identity && decoded.state == RunHostState.TERMINAL)
                            logs.receiveTerminal(decoded)
                            RunHostClientOutcome.Terminal(decoded)
                        }.getOrElse {
                            logs.finishIncomplete("shizuku-runhost-terminal-invalid")
                            RunHostClientOutcome.TransportFailure(RunHostCode.INTERNAL_CONTRACT_FAILURE)
                        },
                    )
                }
            }
            continuation.invokeOnCancellation {
                logs.finishIncomplete("shizuku-runhost-client-cancelled")
                runCatching { host.cancel(identity.runId, identity.generation) }
            }
            val callbackCode = runCatching {
                RunHostCode.fromWireValue(host.setCallback(identity.runId, identity.generation, callback))
            }.getOrElse {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED))
                return@suspendCancellableCoroutine
            }
            if (callbackCode != RunHostCode.OK) {
                finish(RunHostClientOutcome.Rejected(callbackCode))
                return@suspendCancellableCoroutine
            }
            val prepareCode = runCatching { RunHostCode.fromWireValue(host.prepare(request)) }
                .getOrElse {
                    finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED))
                    return@suspendCancellableCoroutine
                }
            if (prepareCode != RunHostCode.OK && prepareCode != RunHostCode.ALREADY_APPLIED) {
                finish(RunHostClientOutcome.Rejected(prepareCode))
                return@suspendCancellableCoroutine
            }
            val startCode = runCatching {
                RunHostCode.fromWireValue(host.start(identity.runId, identity.generation))
            }.getOrElse {
                finish(RunHostClientOutcome.TransportFailure(RunHostCode.HOST_DIED))
                return@suspendCancellableCoroutine
            }
            if (startCode != RunHostCode.OK && startCode != RunHostCode.ALREADY_APPLIED) {
                finish(RunHostClientOutcome.Rejected(startCode))
                return@suspendCancellableCoroutine
            }
            snapshotJob = CoroutineScope(SupervisorJob() + Dispatchers.IO).launch {
                pollRunHostTerminal(host, identity, completed) { outcome -> finish(outcome) }
            }
        }
    }
}

/** Stable connector-only diagnostic for Shizuku client outcomes. */
private fun RunHostClientOutcome.shizukuLogFailureName(): String = when (this) {
    is RunHostClientOutcome.Terminal -> "terminal"
    is RunHostClientOutcome.Rejected -> "rejected-" + code.name.lowercase()
    is RunHostClientOutcome.TransportFailure -> "transport-" + code.name.lowercase()
}

/** Lifecycle seam that keeps Shizuku APIs out of the typed RunHost request/result model. */
internal interface ShizukuRunHostBinding {
    /** Binds one fresh UserService and returns its typed business interface. */
    suspend fun bind(): IRunHost

    /** Removes the exact binding previously returned by [bind]. */
    fun close(host: IRunHost)
}

/** Production Shizuku UserService binding with one unique process identity per run. */
private class AndroidShizukuRunHostBinding(context: Context) : ShizukuRunHostBinding {
    private val applicationContext = context.applicationContext
    private val lock = Any()
    private val active = mutableMapOf<IBinder, BoundService>()

    /** Performs preflight and awaits one typed UserService binder. */
    override suspend fun bind(): IRunHost {
        require(Shizuku.pingBinder()) { "Shizuku binder is unavailable" }
        require(Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) {
            "Shizuku permission is not granted"
        }
        require(Shizuku.getUid() == SHELL_UID) { "Shizuku uid is not shell" }
        require(Shizuku.getSELinuxContext()?.contains(SHELL_DOMAIN) == true) {
            "Shizuku SELinux domain is not shell"
        }
        return withTimeout(BIND_TIMEOUT_MS) { awaitBinding() }
    }

    /** Unbinds and removes one exact UserService instance. */
    override fun close(host: IRunHost) {
        val bound = synchronized(lock) { active.remove(host.asBinder()) } ?: return
        runCatching { Shizuku.unbindUserService(bound.args, bound.connection, true) }
    }

    /** Suspends until one unique UserService connection yields a non-null typed binder. */
    private suspend fun awaitBinding(): IRunHost = suspendCancellableCoroutine { continuation ->
        val token = System.nanoTime().toString(36)
        val args = Shizuku.UserServiceArgs(
            ComponentName(applicationContext.packageName, SERVICE_CLASS),
        )
            .daemon(false)
            .version(BuildConfig.VERSION_CODE)
            .tag("runhost-$token")
            .processNameSuffix("rh" + token.takeLast(8))
        val completed = AtomicBoolean(false)
        lateinit var connection: ServiceConnection

        fun fail(message: String) {
            if (!completed.compareAndSet(false, true)) return
            runCatching { Shizuku.unbindUserService(args, connection, true) }
            if (continuation.isActive) continuation.resumeWithException(IllegalStateException(message))
        }

        connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
                val host = service?.let { binder -> IRunHost.Stub.asInterface(binder) }
                if (host == null) {
                    fail("Shizuku RunHost returned a null binding")
                    return
                }
                if (!completed.compareAndSet(false, true)) {
                    runCatching { Shizuku.unbindUserService(args, this, true) }
                    return
                }
                synchronized(lock) { active[host.asBinder()] = BoundService(args, this) }
                if (continuation.isActive) {
                    continuation.resume(host)
                } else {
                    close(host)
                }
            }

            override fun onServiceDisconnected(name: ComponentName?) = fail("Shizuku RunHost disconnected")

            override fun onBindingDied(name: ComponentName?) = fail("Shizuku RunHost binding died")

            override fun onNullBinding(name: ComponentName?) = fail("Shizuku RunHost returned a null binding")
        }
        continuation.invokeOnCancellation {
            if (completed.compareAndSet(false, true)) {
                runCatching { Shizuku.unbindUserService(args, connection, true) }
            }
        }
        runCatching { Shizuku.bindUserService(args, connection) }
            .onFailure { problem -> fail("Shizuku RunHost bind failed: " + problem::class.java.simpleName) }
    }

    /** One exact args/connection pair owned until [close]. */
    private data class BoundService(
        val args: Shizuku.UserServiceArgs,
        val connection: ServiceConnection,
    )

    private companion object {
        const val SERVICE_CLASS: String = "com.ghostlock.app.shizuku.GhostlockUserService"
        const val SHELL_UID: Int = 2000
        const val SHELL_DOMAIN: String = "u:r:shell:s0"
        const val BIND_TIMEOUT_MS: Long = 10_000L
    }
}
