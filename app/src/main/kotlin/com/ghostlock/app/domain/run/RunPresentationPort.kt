package com.ghostlock.app.domain.run

/** Android presentation-I/O boundary shared by the run coordinator and its UI use case. */
interface RunPresentationPort {
    /** @return Localized message emitted when cancellation is disabled for the active backend. */
    fun abortDisabledMessage(): String

    /**
     * Resolves a stored font reference only when it names a readable app-private file.
     *
     * @param reference Persisted relative font reference, or null for the system default.
     * @return Absolute private path or null when absent, invalid, or outside the private root.
     */
    fun resolveTerminalFont(reference: String?): String?

    /**
     * Publishes the exact current terminal prefix through the Android chooser.
     *
     * @param chooserTitle Localized chooser title supplied by presentation.
     * @return True only when the share intent was built and launched.
     */
    suspend fun shareTerminal(chooserTitle: String): Boolean
}
