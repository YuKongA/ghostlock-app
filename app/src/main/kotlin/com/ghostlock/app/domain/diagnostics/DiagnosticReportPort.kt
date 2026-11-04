package com.ghostlock.app.domain.diagnostics

import java.io.File

/** Boundary for creating one shareable application diagnostic report. */
interface DiagnosticReportPort {
    /**
     * Creates a new immutable report artifact.
     *
     * @return Readable report file owned by the application cache.
     * @throws java.io.IOException When the report cannot be assembled.
     */
    suspend fun create(): File
}
