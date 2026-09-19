package com.ghostlock.app.boot

sealed interface BootRunResult {
    data object Success : BootRunResult

    /** Exploit ran but did not succeed (not rooted). Device is unchanged. */
    data object FailedNotRoot : BootRunResult

    data class Skipped(val reason: SkipReason) : BootRunResult

    /** Unexpected error caught safely — no crash, no repeat attempts beyond limit. */
    data object StoppedSafely : BootRunResult
}

enum class SkipReason {
    Disabled,
    UnsupportedKernel,
    NoCpuPair,
}
