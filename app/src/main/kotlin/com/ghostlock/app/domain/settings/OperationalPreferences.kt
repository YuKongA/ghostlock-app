package com.ghostlock.app.domain.settings

/**
 * Storage-neutral log severity exposed to the settings and run domains.
 *
 * @property token Stable lowercase token rendered by both UI skins.
 */
enum class TerminalLogLevel(val token: String) {
    Verbose("verbose"),
    Debug("debug"),
    Info("info"),
    Warn("warn"),
    Error("error"),
}

/**
 * Operational preferences shared by the settings renderer and run session adapter.
 *
 * @property logLevel Minimum application log severity.
 * @property terminalFontPath Private copied font path, or null for the system monospace font.
 * @property showLogTimestamp Whether terminal rows include elapsed time.
 * @property autoExportLogs Whether each accepted run creates an automatic GLLOG2 document.
 * @property logExportLocation Normalized Downloads-relative export directory.
 * @property warnOnRerun Whether a completed run requires confirmation before rerunning.
 * @property autoAttackOnBoot Whether the dormant boot preference is enabled.
 * @property bootRetryCount Bounded boot retry count in the range 0..10.
 * @note This value contains no Android, Compose, backend, or logging implementation type.
 */
data class OperationalPreferences(
    val logLevel: TerminalLogLevel = TerminalLogLevel.Info,
    val terminalFontPath: String? = null,
    val showLogTimestamp: Boolean = false,
    val autoExportLogs: Boolean = true,
    val logExportLocation: String = DEFAULT_LOG_EXPORT_LOCATION,
    val warnOnRerun: Boolean = true,
    val autoAttackOnBoot: Boolean = false,
    val bootRetryCount: Int = 1,
) {
    companion object {
        /** Historical MediaStore Downloads-relative default retained by R4A. */
        const val DEFAULT_LOG_EXPORT_LOCATION: String = "Download/ghostlock-log"
    }
}
