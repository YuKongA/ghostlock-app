package com.ghostlock.app.backend.backends.diagnose

import com.ghostlock.app.backend.declarations.BackendRuntime
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.domain.backend.BackendAvailability

/** Release-visible diagnostic runtime for the real Flow/JNA/native/shell transport chain. */
internal object DiagnoseBackendRuntime : BackendRuntime {
    /** Diagnose has no resourceful preflight and is available in debug and release builds. */
    override fun availability(): BackendAvailability = BackendAvailability.Ready()

    /** Creates one fresh diagnostic Flow whose binding loads its native library in construction. */
    override fun createFlow(): Flow = DiagnoseFlow()
}
