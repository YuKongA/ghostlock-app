package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.R

/**
 *  CVE-2026-43499 的配置项（multicast 窗口）。
 *
 *  与 43284 分开成文件：两条 backend 的配置项互不影响。
 */
object BackendCve202643499 : BackendSpec {
    override val cve: String = "CVE-2026-43499"
    override val titleRes: Int = R.string.backend_cve_2026_43499

    override fun rows(): List<BackendRow> = Profiles43499.rows()
}
