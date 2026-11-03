package com.ghostlock.app.backend.declarations.cve43284glued

import com.ghostlock.app.R
import com.ghostlock.app.backend.cve43284glued.flow43284GluedRunner
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.ui.backend.BackendFlowRunner
import com.ghostlock.app.ui.backend.BackendReadiness

/** Selectable 43284 backend that launches the unmodified upstream executable. */
object Backend43284Glued : BackendSpec {
    override val key: String = "43284Glued"
    override val cve: String = "CVE-2026-43284-Glued"
    override val titleRes: Int = R.string.backend_cve_2026_43284
    override val displayNameRes: Int = R.string.backend_cve_2026_43284_glued
    override val selectable: Boolean = true
    override val runner: BackendFlowRunner = flow43284GluedRunner()

    /** Reports the executable and route identity shown in the selected backend card. */
    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_channel, "exec (upstream CLI)"),
        BackendRow(R.string.backend_row_payload, "lib43284Glued.so"),
    )

    /** No profile choice is required by this backend card. */
    override fun readiness(): BackendReadiness = BackendReadiness.Ready()
}
