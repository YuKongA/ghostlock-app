package com.ghostlock.app.core.run

import com.ghostlock.app.domain.run.LogSinkBinding
import com.ghostlock.app.domain.run.LogSinkFailure
import com.ghostlock.app.domain.run.LogSinkRequest

/** Creates unopened run-scoped storage sinks before LogHub decides whether to accept the run. */
fun interface LogSinkProvider {
    /**
     * Creates the current private-file sink and optional MediaStore sink.
     *
     * @param request Frozen document identity and export settings.
     * @param onFailure First-failure callback routed back to the matching LogHub run.
     * @return Prebuilt unopened storage bindings; UI is always owned by LogHub.
     * @throws Throwable Only for failures outside the provider's two modeled degradation paths.
     */
    fun create(
        request: LogSinkRequest,
        onFailure: (LogSinkFailure) -> Unit,
    ): List<LogSinkBinding>
}
