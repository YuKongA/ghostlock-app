package com.ghostlock.plugin.core

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotEquals

class PluginMetadataCodecTest {
    @Test
    fun sourceWhitespaceAndFieldOrderDoNotChangeSemanticRoot() {
        val manifest = testManifest()
        val canonical = PluginMetadataCodec.encodeManifest(manifest)
        val fields = Json.parseToJsonElement(canonical).jsonObject.entries.reversed()
        val reordered = fields.joinToString(prefix = "{\n  ", postfix = "\n}", separator = ",\n  ") {
            "${Json.encodeToString(it.key)} : ${it.value}"
        }

        val decodedCanonical = PluginMetadataCodec.decodeManifest(canonical)
        val decodedReordered = PluginMetadataCodec.decodeManifest(reordered)
        assertEquals(decodedCanonical, decodedReordered)
        assertEquals(
            PluginMetadataCodec.pluginContentSha256(decodedCanonical),
            PluginMetadataCodec.pluginContentSha256(decodedReordered),
        )
    }

    @Test
    fun duplicateAndUnknownMembersFailClosed() {
        val canonical = PluginMetadataCodec.encodeManifest(testManifest())
        val duplicate = canonical.replaceFirst("{", "{\"formatMajor\":1,")
        val escapedDuplicate = canonical.replaceFirst("{", "{\"\\u0066ormatMajor\":1,")
        val unknown = canonical.replaceFirst("{", "{\"archiveSha256\":\"deadbeef\",")

        assertFailsWith<IllegalArgumentException> { PluginMetadataCodec.decodeManifest(duplicate) }
        assertFailsWith<IllegalArgumentException> { PluginMetadataCodec.decodeManifest(escapedDuplicate) }
        assertFailsWith<IllegalArgumentException> { PluginMetadataCodec.decodeManifest(unknown) }
    }

    @Test
    fun normalizedManifestSortsSetsAndBindsEveryFileDigest() {
        val manifest = testManifest()
        val decoded = PluginMetadataCodec.decodeManifest(PluginMetadataCodec.encodeManifest(manifest))
        assertEquals(decoded.files.sortedBy { it.path }, decoded.files)
        assertEquals(decoded.nativeLibraries.sortedBy { it.path }, decoded.nativeLibraries)
        assertEquals(
            listOf("libc.so", "libdl.so", "liblog.so", "libm.so"),
            decoded.nativeLibraries.first { it.logicalLoadName == "ghostlock_diagnose" }.neededLibraries,
        )

        val changedMetadata = decoded.copy(versionName = "Diagnose 2")
        val changedFile = decoded.copy(
            files = decoded.files.map {
                if (it.path == "code/classes.dex") it.copy(sha256 = "9".repeat(64)) else it
            },
        )
        assertNotEquals(
            PluginMetadataCodec.pluginContentSha256(decoded),
            PluginMetadataCodec.pluginContentSha256(changedMetadata),
        )
        assertNotEquals(
            PluginMetadataCodec.pluginContentSha256(decoded),
            PluginMetadataCodec.pluginContentSha256(changedFile),
        )
    }

    @Test
    fun nativeAndDexReferencesAreExact() {
        assertFailsWith<IllegalArgumentException> {
            testManifest().copy(
                nativeLibraries = testManifest().nativeLibraries.map {
                    if (it.logicalLoadName == "ghostlock_diagnose") {
                        it.copy(path = "lib/arm64-v8a/missing.so")
                    } else {
                        it
                    }
                },
            ).normalized()
        }
        assertFailsWith<IllegalArgumentException> {
            testManifest().copy(
                files = testManifest().files + PluginFileEntry("code/classes3.dex", "8".repeat(64)),
            ).normalized()
        }
        assertFailsWith<IllegalArgumentException> {
            testManifest().copy(
                files = testManifest().files.filterNot { it.path == "code/classes.dex" },
            ).normalized()
        }
        assertFailsWith<IllegalArgumentException> {
            testManifest().copy(
                files = testManifest().files.filterNot { it.path == "code/classes.dex" } +
                    PluginFileEntry("code/classes999999999999999999999.dex", "8".repeat(64)),
            ).normalized()
        }
    }

    @Test
    fun builtinIndexCarriesOnlySemanticObjectIdentity() {
        val root = PluginMetadataCodec.pluginContentSha256(testManifest())
        val index = BuiltinPluginIndex(
            schemaMajor = 1,
            schemaMinor = 0,
            records = listOf(
                BuiltinPluginIndexRecord(
                    publisherId = "com.ghostlock.firstparty",
                    packageId = "com.ghostlock.diagnose",
                    versionCode = 1,
                    variant = "android-arm64-v8a",
                    assetPath = "ghostlock-plugins/objects/$root.glplugin.zip",
                    pluginContentSha256 = root,
                ),
            ),
            defaultBackendKey = "DIAGNOSE",
        )
        assertEquals(
            index.normalized(),
            PluginMetadataCodec.decodeBuiltinIndex(PluginMetadataCodec.encodeBuiltinIndex(index)),
        )
    }
}

internal fun testManifest(
    publisherId: String = "com.ghostlock.firstparty",
    packageId: String = "com.ghostlock.diagnose",
    versionCode: Long = 1,
): PluginPackageManifest = PluginPackageManifest(
    formatMajor = 1,
    formatMinor = 0,
    publisherId = publisherId,
    packageId = packageId,
    versionCode = versionCode,
    versionName = "Diagnose 1",
    variant = "android-arm64-v8a",
    minAndroidSdk = 31,
    hostApiMajor = 1,
    minHostApiMinor = 0,
    files = listOf(
        PluginFileEntry("licenses/jna/LICENSE", "4".repeat(64)),
        PluginFileEntry("lib/arm64-v8a/libjnidispatch.so", "3".repeat(64)),
        PluginFileEntry("code/classes.dex", "1".repeat(64)),
        PluginFileEntry("lib/arm64-v8a/libghostlock_diagnose.so", "2".repeat(64)),
    ),
    nativeLibraries = listOf(
        PluginNativeLibrary(
            path = "lib/arm64-v8a/libjnidispatch.so",
            abi = "arm64-v8a",
            elfClass = 2,
            elfMachine = 183,
            soname = null,
            logicalLoadName = "jnidispatch",
            neededLibraries = listOf("libm.so", "libc.so", "libdl.so"),
        ),
        PluginNativeLibrary(
            path = "lib/arm64-v8a/libghostlock_diagnose.so",
            abi = "arm64-v8a",
            elfClass = 2,
            elfMachine = 183,
            soname = "libghostlock_diagnose.so",
            logicalLoadName = "ghostlock_diagnose",
            neededLibraries = listOf("libm.so", "liblog.so", "libc.so", "libdl.so"),
        ),
    ),
    components = listOf(
        PluginManifestComponent(
            kind = PluginComponentKind.BACKEND,
            id = "diagnose.backend",
            backendKey = "DIAGNOSE",
            entryContract = "diagnostic-v1",
            entryClass = "com.ghostlock.plugin.diagnose.DiagnoseEntryPoint",
            selectionPolicy = PluginSelectionPolicy.SELECTABLE,
            abortPolicy = PluginAbortPolicy.BEFORE_START_ONLY,
            executionDomains = listOf(PluginExecutionDomain.SHIZUKU_SHELL, PluginExecutionDomain.APP),
            runCapability = PluginRunCapability.DIAGNOSTIC_ONLY,
        ),
    ),
)
