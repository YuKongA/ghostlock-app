package com.ghostlock.app.runhost.appuid

import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Process
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostController
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.runhost.host.AndroidRunHostEnvironment
import com.ghostlock.app.data.runhost.host.AndroidRunHostPreparationAdapter
import com.ghostlock.app.data.runhost.host.RunHostRole
import com.ghostlock.app.data.runhost.wire.RunHostBinderEndpoint
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshotReply
import java.util.concurrent.atomic.AtomicBoolean

/** App-UID execution-domain framework entry for the shared one-shot RunHost implementation. */
class AppUidRunHostService : Service() {
    private val exitCommitted = AtomicBoolean(false)
    private val callbackLock = Any()
    private var callbackBinder: IBinder? = null
    private var callbackDeathRecipient: IBinder.DeathRecipient? = null
    private val controller = RunHostController()
    private val preparation = AndroidRunHostPreparationAdapter(
        role = RunHostRole.APP,
        hostEnvironment = {
            AndroidRunHostEnvironment(
                applicationUid = applicationInfo.uid,
                createWorkspace = { identity, backendKey ->
                    AndroidRunWorkspace.createApp(this, identity, backendKey)
                },
            )
        },
    )
    private val endpoint = RunHostBinderEndpoint(controller, preparation)

    /** Typed binder exported only through the non-exported service binding. */
    private val binder = object : IRunHost.Stub() {
        override fun setCallback(
            runId: String,
            generation: Long,
            callback: IRunHostCallback,
        ): Int {
            if (exitCommitted.get()) return RunHostCode.HOST_DIED.wireValue
            val code = endpoint.setCallback(runId, generation, callback)
            if (code == RunHostCode.OK.wireValue) registerCallbackDeath(callback.asBinder())
            return code
        }

        override fun prepare(request: RunHostRequest): Int = businessCode { endpoint.prepare(request) }

        override fun start(runId: String, generation: Long): Int =
            businessCode { endpoint.start(runId, generation) }

        override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply =
            if (exitCommitted.get()) hostDiedReply() else endpoint.getSnapshot(runId, generation)

        override fun cancel(runId: String, generation: Long): Int =
            businessCode { endpoint.cancel(runId, generation) }

        override fun requestClose(runId: String, generation: Long): Int =
            businessCode { endpoint.requestClose(runId, generation) }

        override fun destroy() {
            if (controller.frameworkDestroyAuthorized()) commitExit()
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onUnbind(intent: Intent?): Boolean {
        commitExit()
        return false
    }

    override fun onDestroy() {
        commitExit()
        super.onDestroy()
    }

    private fun businessCode(call: () -> Int): Int =
        if (exitCommitted.get()) RunHostCode.HOST_DIED.wireValue else call()

    private fun hostDiedReply(): RunHostSnapshotReply = RunHostSnapshotReply().also { reply ->
        reply.code = RunHostCode.HOST_DIED.wireValue
        reply.snapshot = null
    }

    private fun registerCallbackDeath(binder: IBinder) {
        synchronized(callbackLock) {
            callbackDeathRecipient?.let { old ->
                callbackBinder?.let { previous -> runCatching { previous.unlinkToDeath(old, 0) } }
            }
            val recipient = IBinder.DeathRecipient(::commitExit)
            runCatching { binder.linkToDeath(recipient, 0) }
                .onSuccess {
                    callbackBinder = binder
                    callbackDeathRecipient = recipient
                }
                .onFailure { commitExit() }
        }
    }

    /** Commits one physical exit and schedules it after the current framework callback can return. */
    private fun commitExit() {
        if (!exitCommitted.compareAndSet(false, true)) return
        synchronized(callbackLock) {
            callbackDeathRecipient?.let { recipient ->
                callbackBinder?.let { source -> runCatching { source.unlinkToDeath(recipient, 0) } }
            }
            callbackBinder = null
            callbackDeathRecipient = null
        }
        Handler(Looper.getMainLooper()).post {
            stopSelf()
            Process.killProcess(Process.myPid())
        }
    }
}
