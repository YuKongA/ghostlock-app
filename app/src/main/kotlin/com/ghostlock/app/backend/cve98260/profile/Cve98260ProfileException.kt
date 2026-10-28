package com.ghostlock.app.backend.cve98260.profile

/**
 * CVE-2026-98260 profile 解析/校验失败（具名、稳定可检索）。
 *
 * @property reason 稳定 reason（`cve98260-profile:*`）；调用方原样展示/记录，不改写。
 */
internal class Cve98260ProfileException(val reason: String) : Exception(reason) {
    private companion object {
        private const val serialVersionUID = 1L
    }
}

/**
 * CVE-2026-98260 profile 加载失败（assets IO/格式）。
 *
 * @property reason 稳定 reason（`cve98260-profile:*`）。
 */
internal class Cve98260ProfileLoadException(val reason: String, cause: Throwable? = null) :
    Exception(reason, cause) {
    private companion object {
        private const val serialVersionUID = 1L
    }
}
