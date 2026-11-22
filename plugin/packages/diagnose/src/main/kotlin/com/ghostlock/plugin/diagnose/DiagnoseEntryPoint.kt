package com.ghostlock.plugin.diagnose

import com.ghostlock.plugin.api.v1.DiagnosticBackendEntryPoint
import com.ghostlock.plugin.api.v1.DiagnosticBackendRun

/** Resource-free entry instantiated by a one-shot Host classloader. */
class DiagnoseEntryPoint : DiagnosticBackendEntryPoint {
    override fun createRun(): DiagnosticBackendRun = DiagnoseRun()
}
