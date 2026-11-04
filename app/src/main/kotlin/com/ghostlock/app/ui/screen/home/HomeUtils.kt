package com.ghostlock.app.ui.screen.home

import androidx.compose.runtime.Immutable

/** Immutable device and application identity rendered by both home-page skins. */
@Immutable
data class SystemInfo(
    val kernelVersion: String,
    val appVersion: String,
    val deviceModel: String,
    val fingerprint: String,
    val selinuxStatus: String,
    val seccompStatus: Int,
)
