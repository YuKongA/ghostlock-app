package com.ghostlock.app.data.plugin

import com.ghostlock.plugin.core.PluginAbortPolicy
import com.ghostlock.plugin.core.PluginComponentKind
import com.ghostlock.plugin.core.PluginExecutionDomain
import com.ghostlock.plugin.core.PluginFileEntry
import com.ghostlock.plugin.core.PluginManifestComponent
import com.ghostlock.plugin.core.PluginMetadataCodec
import com.ghostlock.plugin.core.PluginNativeLibrary
import com.ghostlock.plugin.core.PluginPackageManifest
import com.ghostlock.plugin.core.PluginRunCapability
import com.ghostlock.plugin.core.PluginSelectionPolicy
import java.io.File
import java.io.FileOutputStream
import java.security.MessageDigest
import java.util.zip.Adler32
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class AndroidPluginArchiveVerifierTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private val verifier = AndroidPluginArchiveVerifier()

    @Test
    fun generatedBuiltinArtifactMatchesIndexAndPassesTheRuntimeVerifier() {
        val assetsRoot = File(
            requireNotNull(System.getProperty(BUILTIN_ASSETS_PROPERTY)) {
                "$BUILTIN_ASSETS_PROPERTY must identify the generated builtin assets directory"
            },
        )
        val index = PluginMetadataCodec.decodeBuiltinIndex(
            File(assetsRoot, "ghostlock-plugins/builtin-index.json").readText(),
        )
        val record = index.records.single()
        val verified = verifier.verify(
            archive = File(assetsRoot, record.assetPath),
            scratchDirectory = scratch(),
            expectedContentRoot = record.pluginContentSha256,
        )

        assertEquals(record.publisherId, verified.manifest.publisherId)
        assertEquals(record.packageId, verified.manifest.packageId)
        assertEquals(record.versionCode, verified.manifest.versionCode)
        assertEquals(record.variant, verified.manifest.variant)
        assertEquals(listOf("code/classes.dex"), verified.dexPaths)
        assertEquals(
            listOf(
                "lib/arm64-v8a/libghostlock_diagnose.so",
                "lib/arm64-v8a/libjnidispatch.so",
            ),
            verified.nativeLibraryPaths,
        )
        assertEquals("DIAGNOSE", index.defaultBackendKey)
    }

    @Test
    fun repackingOrderTimestampCompressionAndCommentDoesNotChangeContentRoot() {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex(),
            "licenses/NOTICE.txt" to "notice\n".encodeToByteArray(),
        )
        val manifest = manifest(payloads)
        val first = archive(
            name = "first.glplugin.zip",
            manifest = manifest,
            payloads = payloads,
            order = listOf("code/classes.dex", "licenses/NOTICE.txt"),
            method = ZipEntry.STORED,
            timestamp = 0L,
            comment = "first packing",
        )
        val second = archive(
            name = "second.glplugin.zip",
            manifest = manifest,
            payloads = payloads,
            order = listOf("licenses/NOTICE.txt", "code/classes.dex"),
            method = ZipEntry.DEFLATED,
            timestamp = 1_700_000_000_000L,
            comment = "ordinary user repack",
            directories = listOf("licenses/"),
        )

        val expectedRoot = PluginMetadataCodec.pluginContentSha256(manifest)
        val firstVerified = verifier.verify(first, scratch(), expectedRoot)
        val secondVerified = verifier.verify(second, scratch(), expectedRoot)

        assertEquals(expectedRoot, firstVerified.pluginContentSha256)
        assertEquals(expectedRoot, secondVerified.pluginContentSha256)
        assertEquals(listOf("code/classes.dex"), secondVerified.dexPaths)
    }

    @Test
    fun payloadWithValidZipCrcButStaleManifestHashIsRejected() {
        val original = linkedMapOf("code/classes.dex" to dex())
        val manifest = manifest(original)
        val changed = linkedMapOf("code/classes.dex" to dex("com.example.Changed"))
        val archive = archive("stale-hash.glplugin.zip", manifest, changed)

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(archive, scratch())
        }
    }

    @Test
    fun damagedStoredEntryIsRejectedByZipIntegrityCheck() {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex(),
            "licenses/NOTICE.txt" to "unique-notice-body".encodeToByteArray(),
        )
        val archive = archive(
            name = "crc-damage.glplugin.zip",
            manifest = manifest(payloads),
            payloads = payloads,
            method = ZipEntry.STORED,
        )
        val bytes = archive.readBytes()
        val marker = "unique-notice-body".encodeToByteArray()
        val index = bytes.indexOfSubsequence(marker)
        check(index >= 0)
        bytes[index] = (bytes[index].toInt() xor 1).toByte()
        archive.writeBytes(bytes)

        assertThrows(Exception::class.java) {
            verifier.verify(archive, scratch())
        }
    }

    @Test
    fun unsafeOrUndeclaredRegularPathIsRejectedBeforeAdmission() {
        val payloads = linkedMapOf("code/classes.dex" to dex())
        val archive = archive(
            name = "unsafe.glplugin.zip",
            manifest = manifest(payloads),
            payloads = payloads,
            extraEntries = mapOf("../outside" to byteArrayOf(1)),
        )

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(archive, scratch())
        }
    }

    @Test
    fun multidexIsReturnedInNumericOrderRegardlessOfZipOrder() {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex("com.example.One"),
            "code/classes2.dex" to dex("com.example.Two"),
            "code/classes3.dex" to dex("com.example.Three"),
        )
        val archive = archive(
            name = "multidex.glplugin.zip",
            manifest = manifest(payloads),
            payloads = payloads,
            order = listOf("code/classes3.dex", "code/classes.dex", "code/classes2.dex"),
        )

        assertEquals(
            listOf("code/classes.dex", "code/classes2.dex", "code/classes3.dex"),
            verifier.verify(archive, scratch()).dexPaths,
        )
    }

    @Test
    fun duplicateClassAcrossDexFilesIsRejected() {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex("com.example.Duplicate"),
            "code/classes2.dex" to dex("com.example.Duplicate"),
        )

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(archive("duplicate-class.glplugin.zip", manifest(payloads), payloads), scratch())
        }
    }

    @Test
    fun duplicateClassInsideOneDexIsRejected() {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex("com.example.Duplicate", "com.example.Duplicate"),
        )

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(archive("duplicate-in-dex.glplugin.zip", manifest(payloads), payloads), scratch())
        }
    }

    @Test
    fun protectedNamespaceDefinitionIsRejected() {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex("com.ghostlock.plugin.api.v1.Impostor"),
        )

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(archive("protected.glplugin.zip", manifest(payloads), payloads), scratch())
        }
    }

    @Test
    fun nativeDeclarationMustMatchElfAndSixteenKiBAlignment() {
        val validPayloads = linkedMapOf(
            "code/classes.dex" to dex(),
            NATIVE_PATH to elf(loadAlignment = 16L * 1024L),
        )
        val validManifest = manifest(validPayloads, native = nativeDeclaration())
        val valid = verifier.verify(
            archive("valid-elf.glplugin.zip", validManifest, validPayloads),
            scratch(),
        )
        assertEquals(listOf(NATIVE_PATH), valid.nativeLibraryPaths)

        val underAlignedPayloads = linkedMapOf(
            "code/classes.dex" to dex(),
            NATIVE_PATH to elf(loadAlignment = 4096),
        )
        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(
                archive(
                    "under-aligned.glplugin.zip",
                    manifest(underAlignedPayloads, native = nativeDeclaration()),
                    underAlignedPayloads,
                ),
                scratch(),
            )
        }

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(
                archive(
                    "wrong-machine.glplugin.zip",
                    manifest(validPayloads, native = nativeDeclaration().copy(elfMachine = 62)),
                    validPayloads,
                ),
                scratch(),
            )
        }
    }

    @Test
    fun wrongExpectedSemanticRootIsRejected() {
        val payloads = linkedMapOf("code/classes.dex" to dex())
        val archive = archive("wrong-root.glplugin.zip", manifest(payloads), payloads)

        assertThrows(IllegalArgumentException::class.java) {
            verifier.verify(
                archive,
                scratch(),
                com.ghostlock.plugin.core.PluginContentSha256("f".repeat(64)),
            )
        }
    }

    private fun scratch(): File = temporaryFolder.newFolder()

    private fun manifest(
        payloads: Map<String, ByteArray>,
        native: PluginNativeLibrary? = null,
    ): PluginPackageManifest = PluginPackageManifest(
        formatMajor = 1,
        formatMinor = 0,
        publisherId = "com.ghostlock.firstparty",
        packageId = "com.ghostlock.diagnose",
        versionCode = 1,
        versionName = "Diagnose 1",
        variant = "android-arm64-v8a",
        minAndroidSdk = 31,
        hostApiMajor = 1,
        minHostApiMinor = 0,
        files = payloads.map { (path, bytes) -> PluginFileEntry(path, sha256(bytes)) },
        nativeLibraries = listOfNotNull(native),
        components = listOf(
            PluginManifestComponent(
                kind = PluginComponentKind.BACKEND,
                id = "diagnose.backend",
                backendKey = "DIAGNOSE",
                entryContract = "diagnostic-v1",
                entryClass = "com.ghostlock.plugin.diagnose.DiagnoseEntryPoint",
                selectionPolicy = PluginSelectionPolicy.SELECTABLE,
                abortPolicy = PluginAbortPolicy.BEFORE_START_ONLY,
                executionDomains = listOf(
                    PluginExecutionDomain.APP,
                    PluginExecutionDomain.SHIZUKU_SHELL,
                ),
                runCapability = PluginRunCapability.DIAGNOSTIC_ONLY,
            ),
        ),
    ).normalized()

    private fun nativeDeclaration(): PluginNativeLibrary = PluginNativeLibrary(
        path = NATIVE_PATH,
        abi = "arm64-v8a",
        elfClass = 2,
        elfMachine = 183,
        soname = null,
        logicalLoadName = "diagnose",
        neededLibraries = emptyList(),
    )

    private fun archive(
        name: String,
        manifest: PluginPackageManifest,
        payloads: Map<String, ByteArray>,
        order: List<String> = payloads.keys.toList(),
        method: Int = ZipEntry.DEFLATED,
        timestamp: Long = 0L,
        comment: String? = null,
        directories: List<String> = emptyList(),
        extraEntries: Map<String, ByteArray> = emptyMap(),
    ): File {
        val output = temporaryFolder.newFile(name)
        ZipOutputStream(FileOutputStream(output)).use { zip ->
            zip.setComment(comment)
            directories.forEach { path ->
                putEntry(zip, path, byteArrayOf(), ZipEntry.STORED, timestamp)
            }
            putEntry(
                zip,
                MANIFEST_PATH,
                PluginMetadataCodec.encodeManifest(manifest).encodeToByteArray(),
                method,
                timestamp,
            )
            order.forEach { path ->
                putEntry(zip, path, requireNotNull(payloads[path]), method, timestamp)
            }
            extraEntries.forEach { (path, bytes) -> putEntry(zip, path, bytes, method, timestamp) }
        }
        return output
    }

    private fun putEntry(
        output: ZipOutputStream,
        path: String,
        bytes: ByteArray,
        method: Int,
        timestamp: Long,
    ) {
        val entry = ZipEntry(path).also {
            it.method = method
            it.time = timestamp
            if (method == ZipEntry.STORED) {
                it.size = bytes.size.toLong()
                it.compressedSize = bytes.size.toLong()
                it.crc = CRC32().apply { update(bytes) }.value
            }
        }
        output.putNextEntry(entry)
        output.write(bytes)
        output.closeEntry()
    }

    private fun dex(vararg binaryClassNames: String): ByteArray {
        val descriptors = binaryClassNames.map { "L${it.replace('.', '/')};" }
        val count = descriptors.size
        val stringIdsOffset = DEX_HEADER_SIZE
        val typeIdsOffset = stringIdsOffset + count * 4
        val classDefsOffset = typeIdsOffset + count * 4
        val dataOffset = classDefsOffset + count * 32
        val encodedStrings = descriptors.map { descriptor ->
            byteArrayOf(descriptor.length.toByte()) + descriptor.encodeToByteArray() + byteArrayOf(0)
        }
        val fileSize = dataOffset + encodedStrings.sumOf(ByteArray::size)
        val output = ByteArray(fileSize)
        "dex\n035\u0000".encodeToByteArray().copyInto(output, 0)
        output.writeUInt32Le(32, fileSize.toLong())
        output.writeUInt32Le(36, DEX_HEADER_SIZE.toLong())
        output.writeUInt32Le(40, 0x12345678)
        output.writeUInt32Le(56, count.toLong())
        output.writeUInt32Le(60, if (count == 0) 0 else stringIdsOffset.toLong())
        output.writeUInt32Le(64, count.toLong())
        output.writeUInt32Le(68, if (count == 0) 0 else typeIdsOffset.toLong())
        output.writeUInt32Le(96, count.toLong())
        output.writeUInt32Le(100, if (count == 0) 0 else classDefsOffset.toLong())
        output.writeUInt32Le(104, (fileSize - dataOffset).toLong())
        output.writeUInt32Le(108, if (count == 0) 0 else dataOffset.toLong())
        var stringOffset = dataOffset
        descriptors.indices.forEach { ordinal ->
            output.writeUInt32Le(stringIdsOffset + ordinal * 4, stringOffset.toLong())
            output.writeUInt32Le(typeIdsOffset + ordinal * 4, ordinal.toLong())
            output.writeUInt32Le(classDefsOffset + ordinal * 32, ordinal.toLong())
            encodedStrings[ordinal].copyInto(output, stringOffset)
            stringOffset += encodedStrings[ordinal].size
        }
        MessageDigest.getInstance("SHA-1").digest(output.copyOfRange(32, output.size)).copyInto(output, 12)
        output.writeUInt32Le(8, Adler32().apply { update(output, 12, output.size - 12) }.value)
        return output
    }

    private fun elf(loadAlignment: Long): ByteArray = ByteArray(ELF_FILE_SIZE).also { output ->
        byteArrayOf(0x7f, 'E'.code.toByte(), 'L'.code.toByte(), 'F'.code.toByte()).copyInto(output)
        output[4] = 2
        output[5] = 1
        output[6] = 1
        output.writeUInt16Le(16, 3)
        output.writeUInt16Le(18, 183)
        output.writeUInt32Le(20, 1)
        output.writeUInt64Le(32, 64)
        output.writeUInt16Le(52, 64)
        output.writeUInt16Le(54, 56)
        output.writeUInt16Le(56, 1)
        output.writeUInt32Le(64, 1)
        output.writeUInt32Le(68, 5)
        output.writeUInt64Le(72, 0)
        output.writeUInt64Le(80, 0)
        output.writeUInt64Le(88, 0)
        output.writeUInt64Le(96, ELF_FILE_SIZE.toLong())
        output.writeUInt64Le(104, ELF_FILE_SIZE.toLong())
        output.writeUInt64Le(112, loadAlignment)
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xff) }

    private companion object {
        const val BUILTIN_ASSETS_PROPERTY = "ghostlock.builtinPluginAssets"
        const val MANIFEST_PATH = "manifest.json"
        const val NATIVE_PATH = "lib/arm64-v8a/libghostlock_diagnose.so"
        const val DEX_HEADER_SIZE = 0x70
        const val ELF_FILE_SIZE = 120
    }
}

private fun ByteArray.writeUInt16Le(offset: Int, value: Int) {
    this[offset] = value.toByte()
    this[offset + 1] = (value ushr 8).toByte()
}

private fun ByteArray.writeUInt32Le(offset: Int, value: Long) {
    repeat(4) { byte -> this[offset + byte] = (value ushr (byte * 8)).toByte() }
}

private fun ByteArray.writeUInt64Le(offset: Int, value: Long) {
    repeat(8) { byte -> this[offset + byte] = (value ushr (byte * 8)).toByte() }
}

private fun ByteArray.indexOfSubsequence(needle: ByteArray): Int {
    if (needle.isEmpty()) return 0
    for (start in 0..size - needle.size) {
        if (needle.indices.all { this[start + it] == needle[it] }) return start
    }
    return -1
}
