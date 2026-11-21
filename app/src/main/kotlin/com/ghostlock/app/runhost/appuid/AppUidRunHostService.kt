package com.ghostlock.app.runhost.appuid

import android.app.Service
import android.content.Intent
import android.os.IBinder
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

/** App-UID execution-domain framework entry for the shared one-shot RunHost implementation. */
class AppUidRunHostService : Service() {
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

    /** Typed binder exported only through the non-exported service binding. */
    private val binder = object : IRunHost.Stub() {
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
            if (controller.frameworkDestroyAuthorized()) stopSelf()
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder
}
