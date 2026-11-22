package com.ghostlock.app.data.plugin

import android.os.Build
import android.os.Process
import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.composition.plugin.PluginCatalogCoordinator
import com.ghostlock.app.core.plugin.PluginCatalogState
import com.ghostlock.app.ui.backend.BackendPresentationRegistry
import com.ghostlock.app.ui.backend.BackendUiCatalog
import com.ghostlock.plugin.core.BuiltinPluginIndex
import com.ghostlock.plugin.core.PluginMetadataCodec
import com.ghostlock.plugin.core.PluginPackageManifest
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Device bootstrap coverage for APK seed, immutable object mode, restart, upgrade, and FAILED gate. */
@RunWith(AndroidJUnit4::class)
class BuiltinPluginBootstrapTest {
    /** First install and restart publish one READY generation; a valid higher version upgrades it. */
    @Test
    fun firstInstallRestartAndUpgradeUseVerifiedImmutableObject() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val root = File(context.cacheDir, "builtin-bootstrap-${System.nanoTime()}")
        try {
            val store = AndroidPluginCatalogStore(root)
            val first = PluginCatalogCoordinator(
                BuiltinPluginSeeder(AndroidBuiltinPluginAssets(context.assets), store, 1),
                Build.VERSION.SDK_INT,
                Process.is64Bit(),
                Os.uname().machine,
            )
            val firstState = first.initialize() as PluginCatalogState.Ready
            val firstRecord = firstState.snapshot.activeRecords.single()
            assertEquals("DIAGNOSE", first.readyDefaultBackendKey())
            assertNotNull(first.readyBackendView()?.resolve("DIAGNOSE"))
            assertEquals(
                0b100000000,
                Os.lstat(objectFile(root, firstRecord.pluginContentSha256.value).absolutePath)
                    .st_mode and 0b111111111,
            )

            val restarted = PluginCatalogCoordinator(
                BuiltinPluginSeeder(AndroidBuiltinPluginAssets(context.assets), store, 1),
                Build.VERSION.SDK_INT,
                Process.is64Bit(),
                Os.uname().machine,
            )
            val restartState = restarted.initialize() as PluginCatalogState.Ready
            assertEquals(firstState.snapshot, restartState.snapshot)

            val upgraded = upgradedAssets(context.assets)
            val upgradeCoordinator = PluginCatalogCoordinator(
                BuiltinPluginSeeder(upgraded.assets, store, 2),
                Build.VERSION.SDK_INT,
                Process.is64Bit(),
                Os.uname().machine,
            )
            val upgradedState = upgradeCoordinator.initialize() as PluginCatalogState.Ready
            val upgradedRecord = upgradedState.snapshot.activeRecords.single()
            assertEquals(2L, upgradedRecord.manifest.versionCode)
            assertEquals(upgraded.contentRoot, upgradedRecord.pluginContentSha256.value)
            assertEquals(2L, upgradedState.snapshot.revision)
            assertEquals(
                0b100000000,
                Os.lstat(objectFile(root, upgraded.contentRoot).absolutePath)
                    .st_mode and 0b111111111,
            )
            assertTrue(!objectFile(root, firstRecord.pluginContentSha256.value).exists())
        } finally {
            cleanup(root)
        }
    }

    /** Invalid builtin metadata publishes FAILED, no membership, and a bounded UI failure reason. */
    @Test
    fun failedBootstrapKeepsUiMembershipEmpty() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val root = File(context.cacheDir, "builtin-failed-${System.nanoTime()}")
        try {
            val assets = BuiltinPluginAssets { path ->
                require(path == BUILTIN_INDEX_PATH)
                ByteArrayInputStream("{}".encodeToByteArray())
            }
            val coordinator = PluginCatalogCoordinator(
                BuiltinPluginSeeder(assets, AndroidPluginCatalogStore(root), 1),
                Build.VERSION.SDK_INT,
                Process.is64Bit(),
                Os.uname().machine,
            )
            val state = coordinator.initialize()
            assertTrue(state is PluginCatalogState.Failed)
            assertNull(coordinator.readyBackendView())
            val catalog = BackendUiCatalog(
                entries = {
                    coordinator.readyBackendView()
                        ?.let(BackendPresentationRegistry.builtin()::entries)
                        .orEmpty()
                },
                selectedKey = { null },
                catalogFailureReason = { (coordinator.state.value as? PluginCatalogState.Failed)?.reason },
                availability = { null },
                text = { "No backend selected" },
                catalogFailureText = { reason -> "catalog-failed: $reason" },
            )
            assertTrue(catalog.entriesSnapshot.isEmpty())
            assertNull(catalog.selectedEntry())
            assertTrue(requireNotNull(catalog.failureStatus()).startsWith("catalog-failed: "))
        } finally {
            cleanup(root)
        }
    }

    /** Repackages the builtin payload with only a normalized version change and matching index root. */
    private fun upgradedAssets(assetManager: android.content.res.AssetManager): UpgradeFixture {
        val rawIndex = assetManager.open(BUILTIN_INDEX_PATH).use { it.readBytes() }
        val index = PluginMetadataCodec.decodeBuiltinIndex(rawIndex.decodeToString())
        val originalRecord = index.records.single()
        val originalArchive = assetManager.open(originalRecord.assetPath).use { it.readBytes() }
        val entries = readZip(originalArchive)
        val originalManifest = PluginMetadataCodec.decodeManifest(
            requireNotNull(entries[MANIFEST_PATH]) { "builtin manifest missing" }.decodeToString(),
        )
        val upgradedManifest = originalManifest.copy(
            versionCode = 2,
            versionName = originalManifest.versionName + " device-upgrade",
        ).normalized()
        val contentRoot = PluginMetadataCodec.pluginContentSha256(upgradedManifest).value
        entries[MANIFEST_PATH] = PluginMetadataCodec.encodeManifest(upgradedManifest).encodeToByteArray()
        val upgradedArchive = writeZip(entries)
        val upgradedRecord = originalRecord.copy(
            versionCode = upgradedManifest.versionCode,
            assetPath = "ghostlock-plugins/objects/$contentRoot.glplugin.zip",
            pluginContentSha256 = com.ghostlock.plugin.core.PluginContentSha256(contentRoot),
        )
        val upgradedIndex = PluginMetadataCodec.encodeBuiltinIndex(
            BuiltinPluginIndex(
                schemaMajor = index.schemaMajor,
                schemaMinor = index.schemaMinor,
                records = listOf(upgradedRecord),
                defaultBackendKey = index.defaultBackendKey,
            ),
        ).encodeToByteArray()
        return UpgradeFixture(
            contentRoot = contentRoot,
            assets = BuiltinPluginAssets { path ->
                when (path) {
                    BUILTIN_INDEX_PATH -> ByteArrayInputStream(upgradedIndex)
                    upgradedRecord.assetPath -> ByteArrayInputStream(upgradedArchive)
                    else -> error("unexpected upgraded asset: $path")
                }
            },
        )
    }

    private fun readZip(bytes: ByteArray): LinkedHashMap<String, ByteArray> {
        val entries = linkedMapOf<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(bytes)).use { input ->
            while (true) {
                val entry = input.nextEntry ?: break
                require(!entry.isDirectory) { "builtin archive contains a directory entry" }
                entries[entry.name] = input.readBytes()
                input.closeEntry()
            }
        }
        return entries
    }

    private fun writeZip(entries: Map<String, ByteArray>): ByteArray =
        ByteArrayOutputStream().use { bytes ->
            ZipOutputStream(bytes).use { output ->
                entries.forEach { (name, content) ->
                    output.putNextEntry(ZipEntry(name))
                    output.write(content)
                    output.closeEntry()
                }
            }
            bytes.toByteArray()
        }

    private fun objectFile(root: File, contentRoot: String): File =
        File(root, "objects/sha256/$contentRoot.glplugin.zip")

    /** Test-only cleanup after store directories have intentionally been restricted to 0500. */
    private fun cleanup(root: File) {
        if (!root.exists()) return
        root.walkTopDown().filter(File::isDirectory).forEach { directory ->
            directory.setReadable(true, true)
            directory.setWritable(true, true)
            directory.setExecutable(true, true)
        }
        root.deleteRecursively()
    }

    private data class UpgradeFixture(
        val contentRoot: String,
        val assets: BuiltinPluginAssets,
    )

    private companion object {
        const val BUILTIN_INDEX_PATH = "ghostlock-plugins/builtin-index.json"
        const val MANIFEST_PATH = "manifest.json"
    }
}
