package com.ghostlock.app.data.plugin

import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/**
 * App-side registry for imported plugins (P1 interface freeze 2026-10-05).
 *
 * [root] is the App's no-backup directory (a plugin .so and its registry must
 * never ride Android auto-backup); the registry itself is the TSV codec in
 * [PluginManifest]. Writes are atomic: the rendered text lands in a sibling
 * temporary file which is fsynced and then moved over the target, so a crash can
 * never leave a half-written registry (the loader would fail closed on a
 * malformed one, but the user would lose the list).
 *
 * The class is deliberately Android-free (it only takes a File), so the registry
 * behaviour is unit-tested on the JVM.
 */
internal class PluginStore(private val root: File) {

    val manifestFile: File get() = File(root, PluginPaths.MANIFEST_FILE)

    /** Missing registry = no plugins; a malformed one throws (fail closed). */
    fun load(): List<PluginManifestEntry> {
        val file = manifestFile
        if (!file.isFile) return emptyList()
        return PluginManifest.parse(file.readText())
    }

    fun save(entries: List<PluginManifestEntry>) {
        require(root.isDirectory || root.mkdirs()) {
            "cannot create the plugin root: " + root.absolutePath
        }
        val temp = File(root, manifestFile.name + ".tmp")
        temp.writeText(PluginManifest.render(entries))
        fsync(temp)
        move(temp, manifestFile)
    }

    /** Adds [entry] or replaces the row with the same id. */
    fun upsert(entry: PluginManifestEntry): List<PluginManifestEntry> {
        val next = load().filterNot { it.id == entry.id } + entry
        save(next)
        return next.sortedBy { it.id }
    }

    fun setEnabled(id: String, enabled: Boolean): List<PluginManifestEntry> {
        val current = load()
        require(current.any { it.id == id }) { "unknown plugin: " + id }
        val next = current.map { if (it.id == id) it.copy(enabled = enabled) else it }
        save(next)
        return next.sortedBy { it.id }
    }

    fun remove(id: String): List<PluginManifestEntry> {
        val next = load().filterNot { it.id == id }
        save(next)
        return next
    }

    private fun fsync(file: File) {
        FileOutputStream(file, true).use { it.fd.sync() }
    }

    private fun move(from: File, to: File) {
        try {
            Files.move(
                from.toPath(),
                to.toPath(),
                StandardCopyOption.REPLACE_EXISTING,
                StandardCopyOption.ATOMIC_MOVE,
            )
        } catch (_: Exception) {
            Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }
    }
}
