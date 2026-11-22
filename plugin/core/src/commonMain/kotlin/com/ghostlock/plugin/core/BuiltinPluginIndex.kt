package com.ghostlock.plugin.core

import kotlinx.serialization.Serializable

@Serializable
data class BuiltinPluginIndex(
    val schemaMajor: Int,
    val schemaMinor: Int,
    val records: List<BuiltinPluginIndexRecord>,
    val defaultBackendKey: String,
) {
    fun normalized(): BuiltinPluginIndex {
        require(schemaMajor == 1) { "unsupported builtin index schemaMajor=$schemaMajor" }
        require(schemaMinor == 0) { "unsupported builtin index schemaMinor=$schemaMinor" }
        requireBackendKey(defaultBackendKey)
        val normalizedRecords = records.map(BuiltinPluginIndexRecord::normalized)
            .sortedWith(compareBy({ it.publisherId }, { it.packageId }, { it.variant }))
        require(normalizedRecords.size == 1) { "builtin index 1.0 requires exactly one record" }
        require(
            normalizedRecords.map { Triple(it.publisherId, it.packageId, it.variant) }.toSet().size ==
                normalizedRecords.size,
        ) { "duplicate builtin package identity and variant" }
        return copy(records = normalizedRecords)
    }
}

@Serializable
data class BuiltinPluginIndexRecord(
    val publisherId: String,
    val packageId: String,
    val versionCode: Long,
    val variant: String,
    val assetPath: String,
    val pluginContentSha256: PluginContentSha256,
) {
    val packageIdentity: PluginPackageIdentity
        get() = PluginPackageIdentity(publisherId, packageId)

    internal fun normalized(): BuiltinPluginIndexRecord {
        requirePluginIdentifier(publisherId, "publisherId")
        requirePluginIdentifier(packageId, "packageId")
        require(versionCode > 0) { "versionCode must be positive" }
        requirePluginIdentifier(variant, "variant")
        requireEntryPath(assetPath, "assetPath")
        require(assetPath == "ghostlock-plugins/objects/$pluginContentSha256.glplugin.zip") {
            "assetPath must identify its semantic content root"
        }
        return this
    }
}
