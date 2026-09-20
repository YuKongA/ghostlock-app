package com.ghostlock.app.data

import java.io.File
import java.nio.charset.StandardCharsets

/**
 * User-imported profiles, kept verbatim in their own folder. Documents are
 * stored exactly as they were picked or parsed (JSON or HOCON text), never
 * rewritten through the converter; the current-layout conversion runs on read
 * and HOCON rendering only happens on export.
 */
internal class UserProfileStore(
    val directory: File,
    private val assetLoader: AssetConfigLoader,
) {
    private companion object {
        const val BuiltinDirectory = "kernel_profiles"
        val IllegalNameCharacters = Regex("[\\\\/:*?\"<>|\\x00-\\x1f]")
    }

    /** One stored document plus the releases its text resolves to. */
    data class StoredProfile(
        val name: String,
        val releases: List<String>,
        val importedAt: Long,
        val sizeBytes: Long,
        val parseError: Boolean,
        /** 1 when the text still needs the legacy-to-current conversion, 2 otherwise. */
        val version: Int,
    )

    class MissingIncludes(val files: List<String>) : Exception()

    private fun files(): List<File> = directory.listFiles().orEmpty()
        .filter { it.isFile }
        .sortedByDescending { it.lastModified() }

    /** Newest first, so the most recently imported document wins. */
    fun list(): List<StoredProfile> {
        val byName = documents()
        return files().map { file ->
            val text = runCatching { file.readText() }.getOrDefault("")
            val entries = runCatching { parseWith(text, byName) }.getOrDefault(emptyList())
            val releases = entries.mapNotNull { it["release"] as? String }
            StoredProfile(
                name = file.name,
                releases = releases,
                importedAt = file.lastModified(),
                sizeBytes = file.length(),
                parseError = releases.isEmpty(),
                version = if (entries.any(::needsConversion)) 1 else 2,
            )
        }
    }

    /**
     * The converter is idempotent, so a document that it would still move or
     * rewrite is an old-layout one; stored text stays untouched either way.
     */
    private fun needsConversion(entry: ValueMap): Boolean {
        val converted = entry.copyValue().asValueMap() ?: return true
        LegacyProfileConverter.convertValue(converted)
        return converted != entry
    }

    fun rawText(name: String): String? {
        val file = fileByName(name) ?: return null
        return runCatching { file.readText() }.getOrNull()
    }

    fun containsRelease(release: String): Boolean = findFileForRelease(release) != null

    /** True when the loaded document [preferredName] carries [release]. */
    fun containsRelease(release: String, preferredName: String?): Boolean {
        val file = preferredName?.let(::fileByName) ?: return false
        return runCatching { fileCarries(file, release) }.getOrDefault(false)
    }

    /**
     * Resolved entry for [release] out of the loaded document [preferredName],
     * converted to the current layout. Conversion is in-memory: the file stays
     * byte-for-byte as imported. Nothing is auto-selected: without an explicit
     * [preferredName] there is no imported layer.
     */
    fun loadEntry(release: String, preferredName: String?): ValueMap? {
        val byName = documents()
        val file = preferredName?.let(::fileByName) ?: return null
        val text = runCatching { file.readText() }.getOrNull() ?: return null
        val entries = runCatching { parseWith(text, byName) }.getOrNull() ?: return null
        return entries.firstOrNull { it["release"] == release }?.also { entry ->
            LegacyProfileConverter.convertValue(entry)
        }
    }

    fun recommendShizuku(release: String, preferredName: String?): Boolean =
        loadEntry(release, preferredName)?.get("recommend_shizuku")
            .let { (it as? Number)?.toLong() == 1L }

    /**
     * True when any stored document recommends Shizuku for [release]. This is
     * kernel metadata, so it applies regardless of which document is loaded.
     */
    fun recommendsShizuku(release: String): Boolean {
        val byName = documents()
        return files().any { file ->
            val text = runCatching { file.readText() }.getOrNull() ?: return@any false
            val entry = runCatching { parseWith(text, byName) }.getOrNull()
                ?.firstOrNull { it["release"] == release }
                ?: return@any false
            LegacyProfileConverter.convertValue(entry)
            (entry["recommend_shizuku"] as? Number)?.toLong() == 1L
        }
    }

    /**
     * Saves a picked document verbatim under a unique name derived from the
     * original file name. Returns the stored file name.
     */
    fun save(originalName: String, text: String): String {
        ensureExists()
        val target = uniqueFile(sanitizeName(originalName))
        target.writeText(text, StandardCharsets.UTF_8)
        return target.name
    }

    fun delete(name: String): Boolean = fileByName(name)?.delete() == true

    /** Replaces an existing document with [text]; false when it is missing. */
    fun overwrite(name: String, text: String): Boolean {
        val file = fileByName(name) ?: return false
        return runCatching {
            file.writeText(text, StandardCharsets.UTF_8)
            true
        }.getOrDefault(false)
    }

    /**
     * Renames a stored document; keeps the old extension when none is given.
     * Returns the resulting file name, or null when it is missing or taken.
     */
    fun rename(name: String, newName: String): String? {
        val file = fileByName(name) ?: return null
        val extension = file.name.substringAfterLast('.', "")
        var sanitized = sanitizeName(newName)
        if (extension.isNotEmpty() && !sanitized.endsWith(".$extension")) {
            sanitized = "$sanitized.$extension"
        }
        if (sanitized == file.name) return file.name
        val target = File(directory, sanitized)
        if (target.exists()) return null
        return if (file.renameTo(target)) target.name else null
    }

    /**
     * Parses a stored document, expands includes and converts every entry, then
     * renders the result as HOCON for export/sharing.
     */
    fun exportHocon(name: String): String? {
        val text = rawText(name) ?: return null
        val entries = runCatching { parseEntries(text) }.getOrNull() ?: return null
        if (entries.isEmpty()) return null
        entries.forEach { LegacyProfileConverter.convertValue(it) }
        return HoconSupport.render(
            if (entries.size == 1) entries.first() else ValueList().apply { addAll(entries) },
        )
    }

    /** Filename (and base name) to verbatim text, for include resolution. */
    fun documents(): Map<String, String> {
        val byName = linkedMapOf<String, String>()
        for (file in files()) {
            val text = runCatching { file.readText() }.getOrNull() ?: continue
            byName[file.name] = text
            byName[file.name.substringAfterLast('/')] = text
        }
        return byName
    }

    /**
     * Parses one document, resolving HOCON includes against the other stored
     * documents first and the bundled assets second. [extraDocuments] are
     * picked files not stored yet (import path). Only entries carrying a
     * `release` are returned; a missing include is reported to the caller.
     */
    fun parseEntries(
        text: String,
        extraDocuments: Map<String, String> = emptyMap(),
    ): List<ValueMap> {
        val byName = linkedMapOf<String, String>().apply {
            putAll(documents())
            for ((name, text) in extraDocuments) {
                put(name, text)
                put(name.substringAfterLast('/'), text)
            }
        }
        return parseWith(text, byName)
    }

    fun findFileForRelease(release: String): File? = findFileForRelease(release, documents())

    private fun fileCarries(file: File, release: String): Boolean {
        val byName = documents()
        return runCatching { parseWith(file.readText(), byName) }.getOrNull()
            ?.any { it["release"] == release } == true
    }

    private fun findFileForRelease(release: String, byName: Map<String, String>): File? =
        files().firstOrNull { file ->
            val text = runCatching { file.readText() }.getOrNull() ?: return@firstOrNull false
            runCatching { parseWith(text, byName) }.getOrNull()
                ?.any { it["release"] == release } == true
        }

    private fun parseWith(text: String, byName: Map<String, String>): List<ValueMap> {
        val expanded = expandIncludes(text, byName, emptyList())
        val entries = ValueList()
        when (val value = HoconSupport.parseValue(expanded)) {
            is Map<*, *> -> value.asValueMap()
                ?.takeIf { it.containsKey("release") }
                ?.let(entries::add)

            is List<*> -> value.forEach { item ->
                item.asValueMap()?.takeIf { it.containsKey("release") }?.let(entries::add)
            }
        }
        return entries.mapNotNull { it.asValueMap() }
    }

    /** Every entry of every stored document, newest document first. */
    fun entries(): List<ValueMap> {
        val byName = documents()
        val all = mutableListOf<ValueMap>()
        for (file in files()) {
            val text = runCatching { file.readText() }.getOrNull() ?: continue
            runCatching { parseWith(text, byName) }.getOrNull()?.let(all::addAll)
        }
        return all
    }

    private fun expandIncludes(
        text: String,
        byName: Map<String, String>,
        visiting: List<String>,
    ): String {
        if (!text.contains("include ")) return text
        val missing = linkedSetOf<String>()
        val expanded = StringBuilder()
        for (line in text.lineSequence()) {
            val trimmed = line.trim()
            if (!trimmed.startsWith("include ")) {
                expanded.append(line).append('\n')
                continue
            }
            val target = trimmed.removePrefix("include ").trim().trim('"')
            val key = target.substringAfterLast('/')
            val local = byName[key]
            if (local != null) {
                if (key !in visiting) {
                    expanded.append(expandIncludes(local, byName, visiting + key))
                }
            } else {
                val asset = assetLoader.load("$BuiltinDirectory/$target")
                if (asset.isBlank()) missing += target else expanded.append(asset)
            }
            expanded.append('\n')
        }
        if (missing.isNotEmpty()) throw MissingIncludes(missing.toList())
        return expanded.toString()
    }

    private fun fileByName(name: String): File? {
        if (name.isEmpty() || name.contains('/') || name.contains('\\')) return null
        return File(directory, name).takeIf { it.isFile }
    }

    private fun sanitizeName(name: String): String {
        val base = name.substringAfterLast('/').substringAfterLast('\\')
        val cleaned = IllegalNameCharacters.replace(base, "_").trim()
        return cleaned.ifEmpty { "profile" }
    }

    private fun uniqueFile(base: String): File {
        val candidate = File(directory, base)
        if (!candidate.exists()) return candidate
        val stem = base.substringBeforeLast('.', base)
        val extension = base.substringAfterLast('.', "").let { if (it == base) "" else ".$it" }
        var index = 1
        while (true) {
            val next = File(directory, "$stem ($index)$extension")
            if (!next.exists()) return next
            index += 1
        }
    }

    private fun ensureExists() {
        if (!directory.isDirectory) directory.mkdirs()
    }
}
