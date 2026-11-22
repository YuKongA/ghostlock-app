package com.ghostlock.app.runhost.shizukushell

import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.annotation.Keep
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

/** Shizuku shell-UID execution-domain framework entry for the shared one-shot RunHost implementation. */
class ShizukuShellRunHostService() : IRunHost.Stub() {
    @Volatile
    private var context: Context? = null
    private val exitCommitted = AtomicBoolean(false)

    private val controller = RunHostController()
    private val preparation = AndroidRunHostPreparationAdapter(
        role = RunHostRole.SHIZUKU_SHELL,
        hostEnvironment = {
            context?.let { liveContext ->
                AndroidRunHostEnvironment(
                    applicationUid = liveContext.applicationInfo.uid,
                    createWorkspace = { identity, backendKey ->
                        AndroidRunWorkspace.createShizukuShell(
                            liveContext.packageName,
                            identity,
                            backendKey,
                        )
                    },
                )
            }
        },
    )
    private val endpoint = RunHostBinderEndpoint(controller, preparation)

    @Keep
    constructor(context: Context) : this() {
        this.context = context
    }

    override fun setCallback(
        runId: String,
        generation: Long,
        callback: IRunHostCallback,
    ): Int = businessCode { endpoint.setCallback(runId, generation, callback) }

    override fun prepare(request: RunHostRequest): Int = businessCode { endpoint.prepare(request) }

    override fun start(runId: String, generation: Long): Int = businessCode {
        endpoint.start(runId, generation)
    }

    override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply =
        if (exitCommitted.get()) hostDiedReply() else endpoint.getSnapshot(runId, generation)

    override fun cancel(runId: String, generation: Long): Int = businessCode {
        endpoint.cancel(runId, generation)
    }

    override fun requestClose(runId: String, generation: Long): Int =
        businessCode { endpoint.requestClose(runId, generation) }

    override fun destroy() {
        if (!controller.frameworkDestroyAuthorized() || !exitCommitted.compareAndSet(false, true)) return
        val exit = Runnable { System.exit(0) }
        val looper = Looper.getMainLooper()
        if (looper != null) Handler(looper).post(exit) else Thread(exit, "ghostlock-runhost-exit").start()
    }

    private fun businessCode(call: () -> Int): Int =
        if (exitCommitted.get()) RunHostCode.HOST_DIED.wireValue else call()

    private fun hostDiedReply(): RunHostSnapshotReply = RunHostSnapshotReply().also { reply ->
        reply.code = RunHostCode.HOST_DIED.wireValue
        reply.snapshot = null
    }
}
