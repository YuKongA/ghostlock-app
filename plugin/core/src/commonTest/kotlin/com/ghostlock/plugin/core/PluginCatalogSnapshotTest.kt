package com.ghostlock.plugin.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class PluginCatalogSnapshotTest {
    @Test
    fun activateIsStableAndRejectsSameVersionDifferentRoot() {
        val first = catalogRecord(testManifest())
        val active = PluginCatalogSnapshot.empty().activate(first)
        assertEquals(1, active.revision)
        assertEquals(listOf(first.normalized()), active.activeRecords)
        val equivalent = active.activate(first)
        assertEquals(active, equivalent)
        assertEquals(active.revision, equivalent.revision)

        val changed = catalogRecord(testManifest().copy(versionName = "different semantic content"))
        assertFailsWith<IllegalArgumentException> { active.activate(changed) }
    }

    @Test
    fun lowerVersionDoesNotDowngradeAndHigherVersionReplacesAtomically() {
        val current = catalogRecord(testManifest(versionCode = 2))
        val active = PluginCatalogSnapshot.empty().activate(current)
        val lower = catalogRecord(testManifest(versionCode = 1))
        assertEquals(active, active.activate(lower))

        val higher = catalogRecord(testManifest(versionCode = 3))
        val upgraded = active.activate(higher)
        assertEquals(2, upgraded.revision)
        assertEquals(3, upgraded.activeRecords.single().manifest.versionCode)
    }

    @Test
    fun activeRecordsHaveUniqueStablePackageVariantKeys() {
        val diagnose = catalogRecord(testManifest())
        val other = catalogRecord(
            testManifest(
                publisherId = "org.example",
                packageId = "org.example.backend",
            ),
        )
        val snapshot = PluginCatalogSnapshot(
            schemaMajor = 1,
            schemaMinor = 0,
            revision = 2,
            activeRecords = listOf(other, diagnose),
        ).normalized()
        assertEquals(
            listOf("com.ghostlock.diagnose", "org.example.backend"),
            snapshot.activeRecords.map { it.manifest.packageId },
        )
        assertFailsWith<IllegalArgumentException> {
            snapshot.copy(activeRecords = listOf(diagnose, diagnose)).normalized()
        }
    }

    @Test
    fun catalogCodecRecomputesManifestRoot() {
        val snapshot = PluginCatalogSnapshot.empty().activate(catalogRecord(testManifest()))
        val encoded = PluginMetadataCodec.encodeCatalogSnapshot(snapshot)
        assertEquals(snapshot.normalized(), PluginMetadataCodec.decodeCatalogSnapshot(encoded))

        val wrongRoot = snapshot.copy(
            activeRecords = snapshot.activeRecords.map {
                it.copy(pluginContentSha256 = PluginContentSha256("f".repeat(64)))
            },
        )
        assertFailsWith<IllegalArgumentException> {
            PluginMetadataCodec.encodeCatalogSnapshot(wrongRoot)
        }
    }

    private fun catalogRecord(manifest: PluginPackageManifest): PluginCatalogRecord {
        val normalized = manifest.normalized()
        return PluginCatalogRecord(
            manifest = normalized,
            pluginContentSha256 = PluginMetadataCodec.pluginContentSha256(normalized),
            provenance = PluginCatalogProvenance(
                kind = PluginCatalogProvenanceKind.BUILTIN_SEED,
                apkVersionCode = 100,
                indexSha256 = "a".repeat(64),
            ),
            enabled = true,
            removable = false,
        )
    }
}
