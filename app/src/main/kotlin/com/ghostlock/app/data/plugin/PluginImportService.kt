package com.ghostlock.app.data.plugin

import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Outcome of one plugin import attempt (domain-visible, see GhostlockRepository). */
sealed interface PluginImportResult {
    /** The module is installed and registered (disabled until the user enables it). */
    data class Imported(
        val entry: PluginManifestEntry,
        val descriptor: PluginDescriptor,
    ) : PluginImportResult

    /** Nothing was installed; [reason] is user-visible. */
    data class Rejected(val reason: String) : PluginImportResult
}

/**
 * P1 import pipeline (interface freeze §3.14.6 / §3.14.7): a picked file is
 * bounded, hashed locally, described by the native probe under that same digest,
 * checked for self-consistency, then installed atomically under
 * `<root>/<id>/<version>/<file>.so` and registered in the manifest.
 *
 * Order is the security contract: the digest is computed BEFORE the probe, and
 * the probe is told to expect it, so a module whose bytes change between hashing
 * and dlopen is rejected natively; the App never moves a file the probe did not
 * describe. A rejection never leaves a file behind (the destination is built
 * from the probe's own id/version).
 *
 * File I/O is limited to [homeDir] (the GHOSTLOCK_HOME equivalent); the probe
 * and the digest are injected, so the JVM tests drive every branch with a fake
 * and real fixtures.
 */
internal class PluginImportService(
    private val homeDir: File,
    private val store: PluginStore,
    private val invoker: PluginProbeInvoker,
    private val sha256: (File) -> String = { Sha256.file(it) },
    private val now: () -> Long = { System.currentTimeMillis() },
) {

    suspend fun import(source: File, displayName: String?): PluginImportResult {
        if (!source.isFile) return reject("the picked file is not readable")
        val size = source.length()
        if (size <= 0L) return reject("the picked file is empty")
        if (size > PluginPaths.MAX_MODULE_BYTES) {
            return reject(
                "the picked file exceeds " + PluginPaths.MAX_MODULE_BYTES / (1024 * 1024) + " MiB",
            )
        }
        val localHash = runCatching { sha256(source) }.getOrElse {
            return reject("cannot hash the picked file: " + (it.message ?: "unknown error"))
        }
        val output = runCatching { invoker.invoke(source.absolutePath, localHash) }.getOrElse {
            return reject("cannot run the native probe: " + (it.message ?: "unknown error"))
        }
        if (output.exitCode != 0) {
            /* The probe rejected the module: report its reason and install nothing. */
            val reason = PluginProbe.rejections(output.stdout).firstOrNull()
                ?: output.stderr.lineSequence().firstOrNull { it.isNotBlank() }?.trim()
                ?: if (output.exitCode < 0) "the probe timed out" else "probe exit " + output.exitCode
            return reject(reason)
        }
        val descriptor = runCatching { PluginProbe.parse(output.stdout) }.getOrElse {
            return reject("the probe output is malformed: " + (it.message ?: "unknown error"))
        }
        /* Self-consistency between the probe's report and the bytes we hold. */
        when {
            !PluginPaths.probeRootMatches(descriptor.countermeasuresRoot) -> {
                return reject(
                    "the probe reports a different plugin root: " + descriptor.countermeasuresRoot,
                )
            }

            descriptor.size != size -> {
                return reject(
                    "the probe reports " + descriptor.size + " bytes, the picked file is " + size,
                )
            }

            !descriptor.sha256.equals(localHash, ignoreCase = true) -> {
                return reject("the probe reports a different SHA-256 than the picked file")
            }

            !PluginPaths.isValidId(descriptor.id) -> {
                return reject("the plugin id is not usable: " + descriptor.id)
            }

            !PluginPaths.isValidVersion(descriptor.version) -> {
                return reject("the plugin version is not usable: " + descriptor.version)
            }

            !descriptor.usable -> {
                return reject(
                    "the host would reject this module: " + descriptor.rejects.joinToString("; "),
                )
            }
        }
        val fileName = sanitizeFileName(displayName) ?: descriptor.id + ".so"
        val relative = PluginPaths.appRelativePath(descriptor.id, descriptor.version, fileName)
        val destination = File(homeDir, relative)
        val parent = destination.parentFile
            ?: return reject("cannot resolve the plugin directory")
        if (!parent.isDirectory && !parent.mkdirs()) {
            return reject("cannot create the plugin directory: " + parent.absolutePath)
        }
        try {
            install(source, destination, parent, fileName)
        } catch (error: Exception) {
            return reject("cannot install the module: " + (error.message ?: "unknown error"))
        }
        val entry = PluginManifestEntry(
            id = descriptor.id,
            version = descriptor.version,
            abiVersion = descriptor.abiVersion,
            sha256 = localHash.lowercase(),
            modulePath = PluginPaths.modulePath(descriptor.id, descriptor.version, fileName),
            enabled = false,
            stage = null,
            importedAtMs = now(),
        )
        val entries = store.upsert(entry)
        return PluginImportResult.Imported(
            entry = entries.first { it.id == entry.id },
            descriptor = descriptor,
        )
    }

    /**
     * Re-describes an installed module for the settings page. The probe runs
     * with the registry's digest, so a module that changed on disk since the
     * import is reported as unavailable instead of being presented as valid.
     * Never throws: the page shows the generic "not described" state instead.
     */
    suspend fun describe(entry: PluginManifestEntry): PluginDescriptor? {
        val fileName = entry.modulePath.substringAfterLast('/')
        if (!PluginPaths.isValidFileName(fileName)) return null
        val file = File(homeDir, PluginPaths.appRelativePath(entry.id, entry.version, fileName))
        if (!file.isFile) return null
        val output = runCatching { invoker.invoke(file.absolutePath, entry.sha256) }.getOrNull()
            ?: return null
        if (output.exitCode != 0) return null
        return runCatching { PluginProbe.parse(output.stdout) }.getOrNull()
    }

    /** Copies into a sibling temp file and renames, so the .so appears atomically. */
    private fun install(source: File, destination: File, parent: File, fileName: String) {
        val temp = File(parent, fileName + ".tmp")
        try {
            source.inputStream().use { input ->
                temp.outputStream().use { output -> input.copyTo(output) }
            }
            if (temp.length() != source.length()) {
                error("short copy: " + temp.length() + " of " + source.length() + " bytes")
            }
            try {
                Files.move(
                    temp.toPath(),
                    destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE,
                )
            } catch (_: Exception) {
                Files.move(
                    temp.toPath(),
                    destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING,
                )
            }
        } finally {
            if (temp.exists()) temp.delete()
        }
    }

    /** A safe, `.so` file name from the picked display name, or null. */
    private fun sanitizeFileName(displayName: String?): String? {
        val base = displayName
            ?.substringAfterLast('/')
            ?.substringAfterLast('\\')
            ?.trim()
            .orEmpty()
        return base.takeIf { PluginPaths.isValidFileName(it) }
    }

    private fun reject(reason: String): PluginImportResult.Rejected =
        PluginImportResult.Rejected(reason)
}
