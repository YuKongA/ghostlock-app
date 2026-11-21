package com.ghostlock.app.runhost.shizukushell

import android.content.Context
import androidx.annotation.Keep
import com.ghostlock.app.composition.backend.BackendRegistry
import com.ghostlock.app.core.runhost.RunHostController
import com.ghostlock.app.data.run.AndroidRunWorkspace
import com.ghostlock.app.data.runhost.host.AndroidRunHostEnvironment
import com.ghostlock.app.data.runhost.host.AndroidRunHostPreparationAdapter
import com.ghostlock.app.data.runhost.host.AndroidRunHostRuntime
import com.ghostlock.app.data.runhost.host.RunHostRole
import com.ghostlock.app.data.runhost.wire.RunHostBinderEndpoint
import com.ghostlock.app.run.IRunHost
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshotReply

/** Shizuku shell-UID execution-domain framework entry for the shared one-shot RunHost implementation. */
class ShizukuShellRunHostService() : IRunHost.Stub() {
    @Volatile
    private var context: Context? = null

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
        resolveRuntime = { key ->
            BackendRegistry.runtime(key)?.let { entry ->
                AndroidRunHostRuntime(
                    descriptor = entry.descriptor,
                    availability = entry::availability,
                    createFlow = entry::createFlow,
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
    ): Int = endpoint.setCallback(runId, generation, callback)

    override fun prepare(request: RunHostRequest): Int = endpoint.prepare(request)

    override fun start(runId: String, generation: Long): Int = endpoint.start(runId, generation)

    override fun getSnapshot(runId: String, generation: Long): RunHostSnapshotReply =
        endpoint.getSnapshot(runId, generation)

    override fun cancel(runId: String, generation: Long): Int = endpoint.cancel(runId, generation)

    override fun requestClose(runId: String, generation: Long): Int =
        endpoint.requestClose(runId, generation)

    override fun destroy() {
        if (controller.frameworkDestroyAuthorized()) System.exit(0)
    }
}
