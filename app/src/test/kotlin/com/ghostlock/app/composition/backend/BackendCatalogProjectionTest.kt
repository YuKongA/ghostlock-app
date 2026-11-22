package com.ghostlock.app.composition.backend

import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.ui.backend.BackendPresentationRegistry
import com.ghostlock.plugin.core.PluginAbortPolicy
import com.ghostlock.plugin.core.PluginCatalogProvenance
import com.ghostlock.plugin.core.PluginCatalogProvenanceKind
import com.ghostlock.plugin.core.PluginCatalogRecord
import com.ghostlock.plugin.core.PluginCatalogSnapshot
import com.ghostlock.plugin.core.PluginComponentKind
import com.ghostlock.plugin.core.PluginExecutionDomain
import com.ghostlock.plugin.core.PluginFileEntry
import com.ghostlock.plugin.core.PluginManifestComponent
import com.ghostlock.plugin.core.PluginMetadataCodec
import com.ghostlock.plugin.core.PluginPackageManifest
import com.ghostlock.plugin.core.PluginRunCapability
import com.ghostlock.plugin.core.PluginSelectionPolicy
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class BackendCatalogProjectionTest {
    @Test
    fun catalogProjectsDescriptorAndExactObjectFromTheSameRecord() {
        val record = record(manifest())
        val view = project(snapshot(record))
        val resolved = requireNotNull(view.resolve("DIAGNOSE"))

        assertEquals(7, view.revision)
        assertEquals("DIAGNOSE", resolved.descriptor.key)
        assertEquals(RunCapability.DIAGNOSTIC_ONLY, resolved.descriptor.runCapability)
        assertEquals(record.pluginContentSha256, resolved.objectRef.pluginContentSha256)
        assertEquals(record.manifest.packageIdentity, resolved.objectRef.packageIdentity)
        assertNull(view.resolve("DEBUG"))
    }

    @Test
    fun duplicateBackendKeyFailsClosed() {
        val first = record(manifest(packageId = "com.ghostlock.first"))
        val second = record(manifest(packageId = "com.ghostlock.second"))
        assertThrows(IllegalArgumentException::class.java) {
            project(snapshot(first, second))
        }
    }

    @Test
    fun duplicateFullComponentIdentityFailsClosed() {
        val first = record(manifest())
        val duplicate = first.copy(
            manifest = first.manifest.copy(
                versionCode = 2,
                versionName = "Diagnose 2",
                variant = "android-arm64-v8a-alt",
                components = first.manifest.components.map { it.copy(backendKey = "DIAGNOSE_ALT") },
            ).normalized(),
        ).let { changed ->
            changed.copy(pluginContentSha256 = PluginMetadataCodec.pluginContentSha256(changed.manifest))
        }
        assertThrows(IllegalArgumentException::class.java) {
            project(snapshot(first, duplicate))
        }
    }

    @Test
    fun presentationOnlyMapsExistingMembership() {
        val view = project(snapshot(record(manifest())))
        val entries = BackendPresentationRegistry.builtin().entries(view)
        assertEquals(listOf("DIAGNOSE"), entries.map { it.descriptor.key })

        val empty = project(PluginCatalogSnapshot.empty())
        assertEquals(emptyList<String>(), BackendPresentationRegistry.builtin().entries(empty).map { it.descriptor.key })
    }

    @Test
    fun incompatiblePluginRemainsVisibleButIsNotReady() {
        val incompatible = record(manifest().copy(minAndroidSdk = 99).normalized())
        val resolved = requireNotNull(project(snapshot(incompatible), sdkInt = 35).resolve("DIAGNOSE"))

        assertTrue(resolved.availability is BackendAvailability.NotReady)
        assertEquals("android-sdk-incompatible", resolved.availability.reasons.single().code)
        assertEquals(incompatible.pluginContentSha256, resolved.objectRef.pluginContentSha256)
    }

    private fun project(
        snapshot: PluginCatalogSnapshot,
        sdkInt: Int = 35,
    ): BackendCatalogView = BackendCatalogProjection.project(
        snapshot,
        sdkInt = sdkInt,
        processIs64Bit = true,
        machine = "aarch64",
    )

    private fun snapshot(vararg records: PluginCatalogRecord): PluginCatalogSnapshot = PluginCatalogSnapshot(
        schemaMajor = 1,
        schemaMinor = 0,
        revision = 7,
        activeRecords = records.toList(),
    )

    private fun record(manifest: PluginPackageManifest): PluginCatalogRecord {
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

    private fun manifest(
        packageId: String = "com.ghostlock.diagnose",
    ): PluginPackageManifest = PluginPackageManifest(
        formatMajor = 1,
        formatMinor = 0,
        publisherId = "com.ghostlock.firstparty",
        packageId = packageId,
        versionCode = 1,
        versionName = "Diagnose 1",
        variant = "android-arm64-v8a",
        minAndroidSdk = 31,
        hostApiMajor = 1,
        minHostApiMinor = 0,
        files = listOf(PluginFileEntry("code/classes.dex", "1".repeat(64))),
        nativeLibraries = emptyList(),
        components = listOf(
            PluginManifestComponent(
                kind = PluginComponentKind.BACKEND,
                id = "diagnose.backend",
                backendKey = "DIAGNOSE",
                entryContract = "diagnostic-v1",
                entryClass = "com.ghostlock.plugin.diagnose.DiagnoseEntryPoint",
                selectionPolicy = PluginSelectionPolicy.SELECTABLE,
                abortPolicy = PluginAbortPolicy.BEFORE_START_ONLY,
                executionDomains = listOf(PluginExecutionDomain.APP, PluginExecutionDomain.SHIZUKU_SHELL),
                runCapability = PluginRunCapability.DIAGNOSTIC_ONLY,
            ),
        ),
    )
}
