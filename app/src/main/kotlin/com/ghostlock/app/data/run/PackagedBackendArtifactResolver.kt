package com.ghostlock.app.data.run

import android.content.Context
import com.ghostlock.app.backend.declarations.BackendCarrierArtifact
import com.ghostlock.app.core.run.OpenedRunInputSource
import java.io.File

/**
 * Resolves a backend's immutable packaged carrier at the Android composition boundary.
 *
 * @param context Application context supplying the installed native-library directory.
 * @param declaration Exact immutable plugin declaration lookup.
 */
class PackagedBackendArtifactResolver internal constructor(
    context: Context,
    private val declaration: (String) -> BackendCarrierArtifact?,
) {
    private val applicationContext = context.applicationContext

    /**
     * Opens the declared carrier once and returns the same-object source lease entry.
     *
     * @param backendKey Immutable registry key.
     * @return Opened packaged carrier; the application never owns deletion of this file.
     * @throws IllegalArgumentException When the declaration or native-library directory is absent.
     */
    fun resolve(backendKey: String): OpenedRunInputSource {
        val artifact = requireNotNull(declaration(backendKey)) { "backend has no carrier declaration" }
        val nativeLibraryDirectory = File(
            requireNotNull(applicationContext.applicationInfo.nativeLibraryDir) {
                "application native-library directory is missing"
            },
        )
        val file = File(nativeLibraryDirectory, artifact.packagedFilename)
        require(file.parentFile == nativeLibraryDirectory) { "carrier escaped native-library directory" }
        return OpenedRunInputSource.open(
            file = file,
            logicalId = artifact.logicalId,
            kind = artifact.kind,
            ownedSource = false,
        )
    }
}
