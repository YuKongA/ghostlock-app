package com.ghostlock.app.backend.core

import java.io.File

/** Builds the backend-scoped environment inherited by a direct Glued executable child.
 *
 * @param backendKey fixed identity selected before process creation.
 * @param runDirectory launcher-owned directory for this single invocation.
 * @return only the allowed 43499 path variables; other backends receive no additions.
 * @note App and Shizuku launchers call the same mapper but retain separate run-directory owners.
 */
internal object ExecutableRunEnvironment {
    /** Returns the allowlisted path variables for one backend invocation.
     *
     * @param backendKey fixed Glued identity.
     * @param runDirectory path owned by the selected App or Shizuku launcher.
     * @return three path variables for 43499-Glued, or an empty map for other backends.
     */
    fun variables(backendKey: String, runDirectory: File): Map<String, String> {
        if (backendKey != "43499-Glued") return emptyMap()
        val home = runDirectory.absolutePath
        return mapOf(
            "GHOSTLOCK_HOME" to home,
            "TMPDIR" to home,
            "GHOSTLOCK_KSU_LOG" to File(runDirectory, ".ghostlock_ksu.log").absolutePath,
        )
    }
}
