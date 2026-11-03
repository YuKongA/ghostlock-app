package com.ghostlock.app.backend.cve43499glued

import android.content.Context
import com.ghostlock.app.backend.core.ExecutableHostProtocol
import com.ghostlock.app.backend.core.ExecutableShizukuHost
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts

/** Shizuku peer adapter for the fixed 43499 Glued executable and input files. */
internal class Glued43499ExecutableHost(context: Context) : ExecutableShizukuHost(
    context = context,
    backendId = ShizukuBackendHosts.CVE_43499_GLUED,
    binaryName = "lib43499-Glued.so",
    allowedFiles = setOf("profile.conf", "profile.bin", "finish.sh", "payload"),
) {
    internal companion object {
        const val KEY_ARGUMENTS = ExecutableHostProtocol.KEY_ARGUMENTS
        const val KEY_FILE_NAMES = ExecutableHostProtocol.KEY_FILE_NAMES
        const val STATE_RUNNING = ExecutableHostProtocol.STATE_RUNNING
        const val STATE_TERMINAL = ExecutableHostProtocol.STATE_TERMINAL

        /** @param name allowlisted basename from the file-name field. */
        fun fileKey(name: String): String = ExecutableHostProtocol.fileKey(name)
    }
}
