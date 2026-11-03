package com.ghostlock.app.backend.core

/** Fixed AIDL payload keys and bounded process-state values for the two Glued executable hosts. */
internal object ExecutableHostProtocol {
    const val KEY_ARGUMENTS = "exec.argv"
    const val KEY_FILE_NAMES = "exec.file_names"
    const val FILE_PREFIX = "exec.file."
    const val STATE_IDLE = 0
    const val STATE_PREPARED = 1
    const val STATE_RUNNING = 2
    const val STATE_TERMINAL = 3
    const val STATE_UNKNOWN = 4

    /** @param name previously allowlisted simple filename. */
    fun fileKey(name: String): String = FILE_PREFIX + name
}
