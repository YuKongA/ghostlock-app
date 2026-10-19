package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.R

/**
 *  CVE-2026-43499 的 profiles（原“kernel profiles”按 CVE 拆分，2026-10-18 用户裁定）。
 */
object Profiles43499 {
    fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, "CVE-2026-43499"),
        BackendRow(R.string.backend_row_privilege, "Shizuku（uid 2000）"),
        BackendRow(R.string.backend_row_status, "当前内核不支持"),
        BackendRow(R.string.backend_row_budget, "6000 ms"),
        BackendRow(R.string.backend_row_cpus, "8"),
    )
}
