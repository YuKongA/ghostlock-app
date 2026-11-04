package com.ghostlock.app.domain.diagnostics

import java.io.File

/** Creates a diagnostic artifact through the injected platform adapter. */
class CreateDiagnosticReportUseCase(
    private val port: DiagnosticReportPort,
) {
    /**
     * Creates one report for save/share UI.
     *
     * @return Newly generated compressed report file.
     * @throws java.io.IOException When the adapter cannot collect or archive the report.
     */
    suspend operator fun invoke(): File = port.create()
}
