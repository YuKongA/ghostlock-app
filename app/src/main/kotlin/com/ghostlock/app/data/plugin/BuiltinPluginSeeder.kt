package com.ghostlock.app.data.plugin

import android.content.res.AssetManager
import com.ghostlock.plugin.core.BuiltinPluginIndexRecord
import com.ghostlock.plugin.core.PluginCatalogProvenance
import com.ghostlock.plugin.core.PluginCatalogProvenanceKind
import com.ghostlock.plugin.core.PluginCatalogRecord
import com.ghostlock.plugin.core.PluginCatalogSnapshot
import com.ghostlock.plugin.core.PluginMetadataCodec
import java.io.InputStream
import java.security.MessageDigest

/** Asset access seam; APK assets and tests both enter the same verifier/store path. */
internal fun interface BuiltinPluginAssets {
    fun open(path: String): InputStream
}

internal class AndroidBuiltinPluginAssets(
    private val assets: AssetManager,
) : BuiltinPluginAssets {
    override fun open(path: String): InputStream = assets.open(path, AssetManager.ACCESS_STREAMING)
}

/** Result retained only for bootstrap composition; the catalog snapshot remains membership authority. */
internal data class BuiltinPluginSeedResult(
    val snapshot: PluginCatalogSnapshot,
    val defaultBackendKey: String,
    val indexSha256: String,
)

/** Installs and activates required APK seeds through the same verified object catalog. */
internal class BuiltinPluginSeeder(
    private val assets: BuiltinPluginAssets,
    private val store: AndroidPluginCatalogStore,
    private val apkVersionCode: Long,
) {
    init {
        require(apkVersionCode > 0) { "APK versionCode must be positive" }
    }

    fun reconcile(): BuiltinPluginSeedResult {
        val rawIndex = assets.open(BUILTIN_INDEX_PATH).use(InputStream::readBytes)
        val indexSha256 = sha256(rawIndex)
        val index = PluginMetadataCodec.decodeBuiltinIndex(rawIndex.decodeToString())
        var next = store.readVerifiedSnapshot()
        index.records.forEach { indexRecord ->
            val installed = assets.open(indexRecord.assetPath).use { source ->
                store.installObject(source, indexRecord.pluginContentSha256)
            }
            requireMatchesIndex(installed, indexRecord)
            val candidate = PluginCatalogRecord(
                manifest = installed.verified.manifest,
                pluginContentSha256 = installed.verified.pluginContentSha256,
                provenance = PluginCatalogProvenance(
                    kind = PluginCatalogProvenanceKind.BUILTIN_SEED,
                    apkVersionCode = apkVersionCode,
                    indexSha256 = indexSha256,
                ),
                enabled = true,
                removable = false,
            )
            next = next.activate(candidate)
        }
        require(
            next.activeRecords.flatMap { it.manifest.components }
                .count { it.backendKey == index.defaultBackendKey } == 1,
        ) { "builtin defaultBackendKey does not identify exactly one active component" }

        val current = store.readVerifiedSnapshot()
        if (next != current) store.commit(next)
        val published = store.readVerifiedSnapshot()
        store.deleteUnreferencedObjects(published)
        return BuiltinPluginSeedResult(
            snapshot = published,
            defaultBackendKey = index.defaultBackendKey,
            indexSha256 = indexSha256,
        )
    }

    private fun requireMatchesIndex(
        installed: StoredPluginObject,
        indexRecord: BuiltinPluginIndexRecord,
    ) {
        val manifest = installed.verified.manifest
        require(manifest.publisherId == indexRecord.publisherId) {
            "builtin manifest publisherId differs from index"
        }
        require(manifest.packageId == indexRecord.packageId) {
            "builtin manifest packageId differs from index"
        }
        require(manifest.versionCode == indexRecord.versionCode) {
            "builtin manifest versionCode differs from index"
        }
        require(manifest.variant == indexRecord.variant) {
            "builtin manifest variant differs from index"
        }
        require(installed.verified.pluginContentSha256 == indexRecord.pluginContentSha256) {
            "builtin manifest semantic content root differs from index"
        }
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString(separator = "") { byte ->
            (byte.toInt() and 0xff).toString(16).padStart(2, '0')
        }

    private companion object {
        const val BUILTIN_INDEX_PATH = "ghostlock-plugins/builtin-index.json"
    }
}
