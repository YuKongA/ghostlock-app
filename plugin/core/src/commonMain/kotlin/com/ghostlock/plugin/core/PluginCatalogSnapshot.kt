package com.ghostlock.plugin.core

import kotlinx.serialization.Serializable

@Serializable
data class PluginCatalogSnapshot(
    val schemaMajor: Int,
    val schemaMinor: Int,
    val revision: Long,
    val activeRecords: List<PluginCatalogRecord>,
) {
    fun normalized(): PluginCatalogSnapshot {
        require(schemaMajor == 1) { "unsupported catalog schemaMajor=$schemaMajor" }
        require(schemaMinor == 0) { "unsupported catalog schemaMinor=$schemaMinor" }
        require(revision >= 0) { "catalog revision must not be negative" }
        val normalizedRecords = activeRecords.map(PluginCatalogRecord::normalized)
            .sortedWith(compareBy({ it.manifest.publisherId }, { it.manifest.packageId }, { it.manifest.variant }))
        require(
            normalizedRecords.map(PluginCatalogRecord::key).toSet().size == normalizedRecords.size,
        ) { "duplicate active package identity and variant" }
        return copy(activeRecords = normalizedRecords)
    }

    fun activate(candidate: PluginCatalogRecord): PluginCatalogSnapshot {
        val current = normalized()
        val normalizedCandidate = candidate.normalized()
        val index = current.activeRecords.indexOfFirst { it.key() == normalizedCandidate.key() }
        if (index < 0) {
            return current.copy(
                revision = current.revision + 1,
                activeRecords = (current.activeRecords + normalizedCandidate)
                    .sortedWith(compareBy({ it.manifest.publisherId }, { it.manifest.packageId }, { it.manifest.variant })),
            )
        }
        val existing = current.activeRecords[index]
        val existingVersion = existing.manifest.versionCode
        val candidateVersion = normalizedCandidate.manifest.versionCode
        if (candidateVersion < existingVersion) return current
        if (candidateVersion == existingVersion &&
            normalizedCandidate.pluginContentSha256 != existing.pluginContentSha256
        ) {
            throw IllegalArgumentException("same plugin version has a different semantic content root")
        }
        if (normalizedCandidate == existing) return current
        val updated = current.activeRecords.toMutableList().also { it[index] = normalizedCandidate }
        return current.copy(revision = current.revision + 1, activeRecords = updated)
    }

    companion object {
        fun empty(): PluginCatalogSnapshot = PluginCatalogSnapshot(
            schemaMajor = 1,
            schemaMinor = 0,
            revision = 0,
            activeRecords = emptyList(),
        )
    }
}

@Serializable
data class PluginCatalogRecord(
    val manifest: PluginPackageManifest,
    val pluginContentSha256: PluginContentSha256,
    val provenance: PluginCatalogProvenance,
    val enabled: Boolean,
    val removable: Boolean,
) {
    internal fun normalized(): PluginCatalogRecord {
        val normalizedManifest = manifest.normalized()
        val actualRoot = PluginMetadataCodec.pluginContentSha256(normalizedManifest)
        require(actualRoot == pluginContentSha256) {
            "catalog semantic content root does not match validated manifest"
        }
        return copy(manifest = normalizedManifest, provenance = provenance.normalized())
    }

    internal fun key(): Triple<String, String, String> = Triple(
        manifest.publisherId,
        manifest.packageId,
        manifest.variant,
    )
}

@Serializable
data class PluginCatalogProvenance(
    val kind: PluginCatalogProvenanceKind,
    val apkVersionCode: Long,
    val indexSha256: String,
) {
    internal fun normalized(): PluginCatalogProvenance {
        require(kind == PluginCatalogProvenanceKind.BUILTIN_SEED) {
            "catalog 1.0 supports BUILTIN_SEED provenance only"
        }
        require(apkVersionCode > 0) { "apkVersionCode must be positive" }
        requireSha256(indexSha256, "indexSha256")
        return this
    }
}

@Serializable
enum class PluginCatalogProvenanceKind {
    BUILTIN_SEED,
}
