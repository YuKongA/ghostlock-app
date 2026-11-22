package com.ghostlock.plugin.core

import kotlinx.serialization.Serializable

private const val MANIFEST_PATH = "manifest.json"

@Serializable
data class PluginPackageManifest(
    val formatMajor: Int,
    val formatMinor: Int,
    val publisherId: String,
    val packageId: String,
    val versionCode: Long,
    val versionName: String,
    val variant: String,
    val minAndroidSdk: Int,
    val hostApiMajor: Int,
    val minHostApiMinor: Int,
    val files: List<PluginFileEntry>,
    val nativeLibraries: List<PluginNativeLibrary>,
    val components: List<PluginManifestComponent>,
) {
    val packageIdentity: PluginPackageIdentity
        get() = PluginPackageIdentity(publisherId, packageId)

    fun normalized(): PluginPackageManifest {
        require(formatMajor == 1) { "unsupported manifest formatMajor=$formatMajor" }
        require(formatMinor == 0) { "unsupported manifest formatMinor=$formatMinor" }
        requirePluginIdentifier(publisherId, "publisherId")
        requirePluginIdentifier(packageId, "packageId")
        require(versionCode > 0) { "versionCode must be positive" }
        requirePrintableUtf8(versionName, "versionName", maximumBytes = 128)
        requirePluginIdentifier(variant, "variant")
        require(minAndroidSdk >= 0) { "minAndroidSdk must not be negative" }
        require(hostApiMajor >= 0) { "hostApiMajor must not be negative" }
        require(minHostApiMinor >= 0) { "minHostApiMinor must not be negative" }

        val normalizedFiles = files.map(PluginFileEntry::normalized).sortedBy(PluginFileEntry::path)
        require(normalizedFiles.isNotEmpty()) { "files must not be empty" }
        requireUnique(normalizedFiles.map(PluginFileEntry::path), "file path")
        require(normalizedFiles.none { it.path == MANIFEST_PATH }) {
            "$MANIFEST_PATH must not appear in files"
        }
        validateDexPaths(normalizedFiles.map(PluginFileEntry::path))

        val normalizedNative = nativeLibraries
            .map(PluginNativeLibrary::normalized)
            .sortedBy(PluginNativeLibrary::path)
        requireUnique(normalizedNative.map(PluginNativeLibrary::path), "native library path")
        requireUnique(normalizedNative.map(PluginNativeLibrary::logicalLoadName), "logicalLoadName")
        val filePaths = normalizedFiles.mapTo(mutableSetOf(), PluginFileEntry::path)
        normalizedNative.forEach { library ->
            require(library.path in filePaths) {
                "native library ${library.path} is not present in files"
            }
        }
        require(
            normalizedFiles.filter { it.path.startsWith("lib/") }.mapTo(mutableSetOf(), PluginFileEntry::path) ==
                normalizedNative.mapTo(mutableSetOf(), PluginNativeLibrary::path),
        ) { "every lib/ regular file must have exactly one nativeLibraries entry" }

        val normalizedComponents = components
            .map(PluginManifestComponent::normalized)
            .sortedBy(PluginManifestComponent::id)
        require(normalizedComponents.size == 1) { "format 1.0 requires exactly one component" }
        requireUnique(normalizedComponents.map(PluginManifestComponent::id), "component id")

        return copy(
            files = normalizedFiles,
            nativeLibraries = normalizedNative,
            components = normalizedComponents,
        )
    }
}

@Serializable
data class PluginFileEntry(
    val path: String,
    val sha256: String,
) {
    internal fun normalized(): PluginFileEntry {
        requireEntryPath(path)
        requireSha256(sha256, "files[$path].sha256")
        return this
    }
}

@Serializable
data class PluginNativeLibrary(
    val path: String,
    val abi: String,
    val elfClass: Int,
    val elfMachine: Int,
    val soname: String?,
    val logicalLoadName: String,
    val neededLibraries: List<String>,
) {
    internal fun normalized(): PluginNativeLibrary {
        requireEntryPath(path)
        require(path.startsWith("lib/")) { "native library path must be under lib/" }
        requirePluginIdentifier(abi, "abi")
        require(elfClass == 1 || elfClass == 2) { "elfClass must be ELFCLASS32(1) or ELFCLASS64(2)" }
        require(elfMachine > 0) { "elfMachine must be positive" }
        soname?.let { requirePluginIdentifier(it, "soname") }
        requirePluginIdentifier(logicalLoadName, "logicalLoadName")
        val normalizedNeeded = neededLibraries.sorted()
        requireUnique(normalizedNeeded, "needed library")
        normalizedNeeded.forEach { requirePluginIdentifier(it, "neededLibrary") }
        return copy(neededLibraries = normalizedNeeded)
    }
}

@Serializable
data class PluginManifestComponent(
    val kind: PluginComponentKind,
    val id: String,
    val backendKey: String,
    val entryContract: String,
    val entryClass: String,
    val selectionPolicy: PluginSelectionPolicy,
    val abortPolicy: PluginAbortPolicy,
    val executionDomains: List<PluginExecutionDomain>,
    val runCapability: PluginRunCapability,
) {
    val componentId: String
        get() = id

    internal fun normalized(): PluginManifestComponent {
        require(kind == PluginComponentKind.BACKEND) { "format 1.0 supports BACKEND only" }
        requirePluginIdentifier(id, "componentId")
        requireBackendKey(backendKey)
        requirePluginIdentifier(entryContract, "entryContract")
        requireJavaBinaryName(entryClass)
        val normalizedDomains = executionDomains.sortedBy(Enum<*>::name)
        require(normalizedDomains.isNotEmpty()) { "executionDomains must not be empty" }
        requireUnique(normalizedDomains, "execution domain")
        return copy(executionDomains = normalizedDomains)
    }
}

@Serializable
enum class PluginComponentKind {
    BACKEND,
}

@Serializable
enum class PluginSelectionPolicy {
    SELECTABLE,
}

@Serializable
enum class PluginAbortPolicy {
    BEFORE_START_ONLY,
}

@Serializable
enum class PluginExecutionDomain {
    APP,
    SHIZUKU_SHELL,
}

@Serializable
enum class PluginRunCapability {
    DIAGNOSTIC_ONLY,
}

private val dexPathPattern = Regex("code/classes(?:([2-9][0-9]*))?\\.dex")

private fun validateDexPaths(paths: List<String>) {
    val dexPaths = paths.filter { it.startsWith("code/") && it.endsWith(".dex") }
    require("code/classes.dex" in dexPaths) { "code/classes.dex is required" }
    val indices = dexPaths.map { path ->
        val match = dexPathPattern.matchEntire(path)
            ?: throw IllegalArgumentException("invalid DEX path: $path")
        match.groupValues[1].takeIf(String::isNotEmpty)?.let { ordinal ->
            ordinal.toIntOrNull()
                ?: throw IllegalArgumentException("DEX ordinal is outside the supported integer range: $path")
        } ?: 1
    }.sorted()
    require(indices == (1..indices.last()).toList()) { "DEX paths must be contiguous" }
}

private fun <T> requireUnique(values: List<T>, label: String) {
    require(values.toSet().size == values.size) { "duplicate $label" }
}
