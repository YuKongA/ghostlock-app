package com.ghostlock.app.domain.update

/** Storage- and transport-neutral application update result. */
data class AppUpdateInfo(
    val versionCode: Long = 0L,
    val downloadUrl: String = "",
    val changelog: String = "",
)
