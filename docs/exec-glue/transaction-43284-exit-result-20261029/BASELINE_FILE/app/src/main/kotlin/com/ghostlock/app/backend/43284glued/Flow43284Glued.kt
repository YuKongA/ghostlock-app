package com.ghostlock.app.backend.cve43284glued

import android.content.Context
import com.ghostlock.app.core.flow.FlowRequest
import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.backend.core.ExecutableBackendProcess
import com.ghostlock.app.ui.backend.BackendFlowRunner
import com.ghostlock.app.ui.backend.BackendRunUiState

/** 43284 exec request containing the app context and the output-line sink. */
internal data class Flow43284GluedRequest(val context: Context, val onLine: (String) -> Unit) : FlowRequest

/** Production 43284 glue flow: one ordered exec step. */
internal class Flow43284Glued : SequentialFlow<Flow43284GluedRequest>(Flow43284Glued::class) {
    override fun steps(request: Flow43284GluedRequest): List<FlowStep> = listOf(FlowStep("exec") {
        when (val state = ExecutableBackendProcess.run(
            context = request.context,
            backendKey = "43284Glued",
            binaryName = "lib43284Glued.so",
            arguments = emptyList(),
            onLine = request.onLine,
        )) {
            is BackendRunUiState.Finished -> StepResult.Done(mapOf("exit" to "finished"))
            is BackendRunUiState.Refused -> StepResult.Failed(state.reasons.joinToString(","))
            else -> StepResult.Failed("exec-state-not-finished")
        }
    })
}

/** Creates the spec-owned runner that constructs one fresh 43284 SequentialFlow per invocation. */
internal fun flow43284GluedRunner(): BackendFlowRunner = object : BackendFlowRunner {
    override val key: String = "43284Glued"
    override suspend fun run(context: Context, onLine: (String) -> Unit) =
        BackendRunUiState.Finished(Flow43284Glued().run(Flow43284GluedRequest(context, onLine)))
}
