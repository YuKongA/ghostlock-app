package com.ghostlock.app.data.plugin

import com.ghostlock.plugin.core.BuiltinPluginIndex
import com.ghostlock.plugin.core.BuiltinPluginIndexRecord
import com.ghostlock.plugin.core.PluginMetadataCodec
import java.io.ByteArrayInputStream
import java.io.File
import java.io.FileInputStream
import java.io.InputStream
import java.util.zip.ZipEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.rules.TemporaryFolder
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BuiltinPluginSeederTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun cleanInstallAndRestartPublishOneStableCatalogGeneration() {
        val root = temporaryFolder.newFolder("clean")
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "clean.glplugin.zip")
        val assets = assetsFor(fixture)
        val store = AndroidPluginCatalogStore(root)
        val seeder = BuiltinPluginSeeder(assets, store, apkVersionCode = 100)

        val first = seeder.reconcile()
        val second = seeder.reconcile()

        assertEquals("DIAGNOSE", first.defaultBackendKey)
        assertEquals(1, first.snapshot.activeRecords.size)
        assertEquals(fixture.contentRoot, first.snapshot.activeRecords.single().pluginContentSha256)
        assertEquals(first.snapshot, second.snapshot)
        assertEquals(1L, second.snapshot.revision)
    }

    @Test
    fun higherVersionInstallsThenAtomicallyReplacesActiveRecord() {
        val root = temporaryFolder.newFolder("upgrade")
        val store = AndroidPluginCatalogStore(root)
        val first = TestPluginArchiveFactory.create(
            temporaryFolder,
            "upgrade-v1.glplugin.zip",
            versionCode = 1,
        )
        BuiltinPluginSeeder(assetsFor(first), store, apkVersionCode = 100).reconcile()
        val second = TestPluginArchiveFactory.create(
            temporaryFolder,
            "upgrade-v2.glplugin.zip",
            versionCode = 2,
            className = "com.ghostlock.plugin.diagnose.VersionTwo",
        )

        val upgraded = BuiltinPluginSeeder(assetsFor(second), store, apkVersionCode = 101).reconcile()

        assertEquals(2L, upgraded.snapshot.activeRecords.single().manifest.versionCode)
        assertEquals(second.contentRoot, upgraded.snapshot.activeRecords.single().pluginContentSha256)
        assertEquals(2L, upgraded.snapshot.revision)
        assertEquals(
            listOf("${second.contentRoot}.glplugin.zip"),
            File(root, "objects/sha256").listFiles().orEmpty().map(File::getName),
        )
    }

    @Test
    fun sameVersionAndRootRepackIsEquivalentButDifferentRootConflicts() {
        val root = temporaryFolder.newFolder("same-version")
        val store = AndroidPluginCatalogStore(root)
        val first = TestPluginArchiveFactory.create(
            temporaryFolder,
            "same-first.glplugin.zip",
            method = ZipEntry.STORED,
        )
        val firstResult = BuiltinPluginSeeder(assetsFor(first), store, apkVersionCode = 100).reconcile()
        val repacked = TestPluginArchiveFactory.create(
            temporaryFolder,
            "same-repacked.glplugin.zip",
            method = ZipEntry.DEFLATED,
            reverseOrder = true,
            timestamp = 1_700_000_000_000L,
        )
        val repackedResult = BuiltinPluginSeeder(
            assetsFor(repacked),
            store,
            apkVersionCode = 100,
        ).reconcile()
        assertEquals(firstResult.snapshot, repackedResult.snapshot)

        val conflicting = TestPluginArchiveFactory.create(
            temporaryFolder,
            "same-conflict.glplugin.zip",
            className = "com.ghostlock.plugin.diagnose.Changed",
        )
        assertThrows(IllegalArgumentException::class.java) {
            BuiltinPluginSeeder(assetsFor(conflicting), store, apkVersionCode = 100).reconcile()
        }
        assertEquals(first.contentRoot, store.readVerifiedSnapshot().activeRecords.single().pluginContentSha256)
        assertEquals(2, File(root, "objects/sha256").listFiles().orEmpty().size)
    }

    @Test
    fun successfulReconcileCollectsObjectLeftBeforeCatalogCommit() {
        val root = temporaryFolder.newFolder("crash-orphan")
        val store = AndroidPluginCatalogStore(root)
        val orphan = TestPluginArchiveFactory.create(
            temporaryFolder,
            "orphan-before-commit.glplugin.zip",
            versionCode = 2,
            packageId = "com.ghostlock.orphan",
        )
        FileInputStream(orphan.archive).use { store.installObject(it, orphan.contentRoot) }
        val active = TestPluginArchiveFactory.create(temporaryFolder, "active-after-crash.glplugin.zip")

        BuiltinPluginSeeder(assetsFor(active), store, apkVersionCode = 100).reconcile()

        assertEquals(
            listOf("${active.contentRoot}.glplugin.zip"),
            File(root, "objects/sha256").listFiles().orEmpty().map(File::getName),
        )
    }

    @Test
    fun catalogCorruptionAndIndexIdentityMismatchFailClosed() {
        val corruptRoot = temporaryFolder.newFolder("corrupt")
        File(corruptRoot, "catalog.json").writeText("not-json")
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "corrupt-seed.glplugin.zip")
        assertThrows(IllegalArgumentException::class.java) {
            BuiltinPluginSeeder(
                assetsFor(fixture),
                AndroidPluginCatalogStore(corruptRoot),
                apkVersionCode = 100,
            ).reconcile()
        }

        val wrongRecord = indexRecord(fixture).copy(versionCode = 99)
        val wrongIndex = BuiltinPluginIndex(1, 0, listOf(wrongRecord), "DIAGNOSE")
        val wrongAssets = MapBuiltinAssets(
            mapOf(
                INDEX_PATH to PluginMetadataCodec.encodeBuiltinIndex(wrongIndex).encodeToByteArray(),
                wrongRecord.assetPath to fixture.archive.readBytes(),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            BuiltinPluginSeeder(
                wrongAssets,
                AndroidPluginCatalogStore(temporaryFolder.newFolder("identity-mismatch")),
                apkVersionCode = 100,
            ).reconcile()
        }
    }

    @Test
    fun explicitDefaultMustNameAnInstalledBackend() {
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "bad-default.glplugin.zip")
        val index = BuiltinPluginIndex(1, 0, listOf(indexRecord(fixture)), "MISSING")
        val assets = MapBuiltinAssets(
            mapOf(
                INDEX_PATH to PluginMetadataCodec.encodeBuiltinIndex(index).encodeToByteArray(),
                index.records.single().assetPath to fixture.archive.readBytes(),
            ),
        )

        val problem = assertThrows(IllegalArgumentException::class.java) {
            BuiltinPluginSeeder(
                assets,
                AndroidPluginCatalogStore(temporaryFolder.newFolder("bad-default-root")),
                apkVersionCode = 100,
            ).reconcile()
        }
        assertTrue(problem.message.orEmpty().contains("defaultBackendKey"))
    }

    private fun assetsFor(fixture: TestPluginArchive): BuiltinPluginAssets {
        val record = indexRecord(fixture)
        val index = BuiltinPluginIndex(1, 0, listOf(record), "DIAGNOSE")
        return MapBuiltinAssets(
            mapOf(
                INDEX_PATH to PluginMetadataCodec.encodeBuiltinIndex(index).encodeToByteArray(),
                record.assetPath to fixture.archive.readBytes(),
            ),
        )
    }

    private fun indexRecord(fixture: TestPluginArchive): BuiltinPluginIndexRecord =
        BuiltinPluginIndexRecord(
            publisherId = fixture.manifest.publisherId,
            packageId = fixture.manifest.packageId,
            versionCode = fixture.manifest.versionCode,
            variant = fixture.manifest.variant,
            assetPath = "ghostlock-plugins/objects/${fixture.contentRoot}.glplugin.zip",
            pluginContentSha256 = fixture.contentRoot,
        )

    private companion object {
        const val INDEX_PATH = "ghostlock-plugins/builtin-index.json"
    }
}

private class MapBuiltinAssets(
    private val values: Map<String, ByteArray>,
) : BuiltinPluginAssets {
    override fun open(path: String): InputStream = ByteArrayInputStream(
        requireNotNull(values[path]) { "missing test asset $path" },
    )
}
