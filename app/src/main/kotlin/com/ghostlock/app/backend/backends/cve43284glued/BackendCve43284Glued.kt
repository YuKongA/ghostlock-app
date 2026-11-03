package com.ghostlock.app.backend.backends.cve43284glued

import com.ghostlock.app.R
import com.ghostlock.app.core.flow.FlowSymbol
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.ui.backend.BackendReadiness

/** Selectable 43284 backend that launches the unmodified upstream executable. */
object BackendCve43284Glued : BackendSpec {
    override val key: String = "43284Glued"
    override val cve: String = "CVE-2026-43284-Glued"
    override val titleRes: Int = R.string.backend_cve_2026_43284
    override val displayNameRes: Int = R.string.backend_cve_2026_43284_glued
    override val selectable: Boolean = true

    /** B4b：context-aware 反射入口（host 进程按此符号执行；需 host 提供 Context）。 */
    override val flowSymbol: FlowSymbol = FlowSymbol(
        "com.ghostlock.app.backend.backends.cve43284glued.Flow43284GluedEntry",
        "entry",
    )

    /** Reports the executable and route identity shown in the selected backend card. */
    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_channel, "exec (upstream CLI)"),
        BackendRow(R.string.backend_row_payload, "lib43284Glued.so"),
    )

    /** No profile choice is required by this backend card. */
    override fun readiness(): BackendReadiness = BackendReadiness.Ready()
}
