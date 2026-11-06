package com.ghostlock.app.data.run

import android.content.Context
import android.content.pm.PackageManager
import android.system.Os
import com.ghostlock.app.core.run.OpenedRunInputSource
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadRootManagers
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.io.File

/** Opened manager executable plus the exact argv frozen for one request. */
data class ResolvedRootManagerArtifact(
    val source: OpenedRunInputSource,
    val args: List<String>,
)

/**
 * Resolves a supported installed root-manager executable without root-side path discovery.
 *
 * @param context Application context used only for PackageManager lookup.
 * @param releaseProvider Kernel release provider injected for deterministic source tests.
 */
class RootManagerArtifactResolver internal constructor(
    context: Context,
    private val releaseProvider: () -> String = { Os.uname().release },
) {
    private val packageManager = context.applicationContext.packageManager

    /**
     * Opens the first installed declared package's exact native executable and freezes KMI argv.
     *
     * @param definition Frozen Payload authority.
     * @return Same-object executable source and exact direct argv.
     * @throws IllegalArgumentException For launch-only/unknown managers or an unparseable release.
     */
    fun resolve(definition: PayloadDefinition): ResolvedRootManagerArtifact {
        val manager = requireNotNull(PayloadRootManagers.find(definition.rootManager)) {
            "unknown root manager"
        }
        require(!manager.launchOnly && manager.binary.isNotBlank()) {
            "launch-only root manager has no staged executable"
        }
        val executable = manager.packages.firstNotNullOfOrNull { packageName ->
            val application = runCatching {
                packageManager.getApplicationInfo(packageName, PackageManager.ApplicationInfoFlags.of(0L))
            }.recoverCatching {
                @Suppress("DEPRECATION")
                packageManager.getApplicationInfo(packageName, 0)
            }.getOrNull() ?: return@firstNotNullOfOrNull null
            val directory = application.nativeLibraryDir?.let(::File)
                ?: return@firstNotNullOfOrNull null
            File(directory, manager.binary).takeIf { candidate -> candidate.parentFile == directory }
        } ?: throw IllegalStateException("root manager executable is unavailable")
        val kmi = parseKmi(releaseProvider())
        val args = buildList {
            addAll(manager.args)
            add("--kmi")
            add(kmi)
            if (definition.safeMode) add("--safe-mode")
        }
        return ResolvedRootManagerArtifact(
            source = OpenedRunInputSource.open(
                file = executable,
                logicalId = ROOT_MANAGER_ARTIFACT_ID,
                kind = RunInputArtifactKind.EXECUTABLE,
                ownedSource = false,
            ),
            args = args,
        )
    }

    /** Returns canonical `androidN-X.Y` or rejects releases without both components. */
    private fun parseKmi(release: String): String {
        val android = Regex("(?:^|[-_])(android[0-9]+)(?:[-_.]|$)")
            .find(release)?.groupValues?.get(1)
            ?: throw IllegalArgumentException("kernel release has no Android generation")
        val version = Regex("^([0-9]+)\\.([0-9]+)(?:\\.|[-_]|$)")
            .find(release)
            ?: throw IllegalArgumentException("kernel release has no major/minor version")
        return android + "-" + version.groupValues[1] + "." + version.groupValues[2]
    }

    companion object {
        /** Single executable artifact shared by every RootManager Payload item in one run. */
        const val ROOT_MANAGER_ARTIFACT_ID: String = "root-manager-executable"
    }
}
