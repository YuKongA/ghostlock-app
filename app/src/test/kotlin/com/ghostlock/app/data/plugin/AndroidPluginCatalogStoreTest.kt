package com.ghostlock.app.data.plugin

import androidx.test.core.app.ApplicationProvider
import com.ghostlock.app.composition.backend.PluginObjectRef
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
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.security.MessageDigest
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermission
import java.util.zip.Adler32
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
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
class AndroidPluginCatalogStoreTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun objectIsFsyncedReadOnlyAndReverifiedBeforeAtomicCatalogReference() {
        val root = temporaryFolder.newFolder("catalog")
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "source.glplugin.zip")
        val store = AndroidPluginCatalogStore(root)

        val installed = FileInputStream(fixture.archive).use { source ->
            store.installObject(source, fixture.contentRoot)
        }
        assertTrue(installed.file.isFile)
        assertEquals(
            setOf(PosixFilePermission.OWNER_READ),
            Files.getPosixFilePermissions(installed.file.toPath()),
        )
        assertTrue(store.readVerifiedSnapshot().activeRecords.isEmpty())

        val snapshot = PluginCatalogSnapshot.empty().activate(record(fixture, apkVersionCode = 7))
        store.commit(snapshot)
        assertEquals(snapshot, store.readVerifiedSnapshot())

        val source = PluginObjectSource(store)
        val ref = PluginObjectRef(
            packageIdentity = fixture.manifest.packageIdentity,
            variant = fixture.manifest.variant,
            versionCode = fixture.manifest.versionCode,
            pluginContentSha256 = fixture.contentRoot,
        )
        source.open(ref).use { lease ->
            lease.duplicate().use { duplicate ->
                val prefix = ByteArray(4)
                android.os.ParcelFileDescriptor.AutoCloseInputStream(duplicate).use { input ->
                    assertEquals(prefix.size, input.read(prefix))
                }
                assertArrayEquals(byteArrayOf(0x50, 0x4b, 0x03, 0x04), prefix)
            }
        }
    }

    @Test
    fun sameSemanticObjectWithDifferentZipPackingReusesExistingPhysicalBytes() {
        val root = temporaryFolder.newFolder("repack-catalog")
        val first = TestPluginArchiveFactory.create(
            temporaryFolder,
            "first.glplugin.zip",
            method = ZipEntry.STORED,
            timestamp = 0L,
        )
        val second = TestPluginArchiveFactory.create(
            temporaryFolder,
            "second.glplugin.zip",
            method = ZipEntry.DEFLATED,
            timestamp = 1_700_000_000_000L,
            reverseOrder = true,
        )
        assertEquals(first.contentRoot, second.contentRoot)
        assertFalse(first.archive.readBytes().contentEquals(second.archive.readBytes()))
        val store = AndroidPluginCatalogStore(root)
        val installed = FileInputStream(first.archive).use { store.installObject(it, first.contentRoot) }
        val retainedBytes = installed.file.readBytes()

        val reused = FileInputStream(second.archive).use { store.installObject(it, second.contentRoot) }

        assertEquals(installed.file, reused.file)
        assertArrayEquals(retainedBytes, reused.file.readBytes())
    }

    @Test
    fun unreferencedObjectIsAnOrphanUntilStartupCollection() {
        val root = temporaryFolder.newFolder("orphan-catalog")
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "orphan.glplugin.zip")
        val store = AndroidPluginCatalogStore(root)
        val installed = FileInputStream(fixture.archive).use { store.installObject(it, fixture.contentRoot) }
        assertTrue(installed.file.exists())

        store.deleteUnreferencedObjects(PluginCatalogSnapshot.empty())

        assertFalse(installed.file.exists())
    }

    @Test
    fun corruptedObjectAndCatalogBothFailClosed() {
        val objectRoot = temporaryFolder.newFolder("corrupt-object-catalog")
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "corrupt-source.glplugin.zip")
        val objectStore = AndroidPluginCatalogStore(objectRoot)
        val installed = FileInputStream(fixture.archive).use {
            objectStore.installObject(it, fixture.contentRoot)
        }
        Files.setPosixFilePermissions(
            installed.file.toPath(),
            setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE),
        )
        val bytes = installed.file.readBytes()
        bytes[0] = (bytes[0].toInt() xor 1).toByte()
        installed.file.writeBytes(bytes)
        Files.setPosixFilePermissions(installed.file.toPath(), setOf(PosixFilePermission.OWNER_READ))
        assertThrows(Exception::class.java) {
            objectStore.reopenVerifiedObject(fixture.contentRoot)
        }

        val catalogRoot = temporaryFolder.newFolder("corrupt-catalog")
        File(catalogRoot, "catalog.json").writeText("{not-json")
        assertThrows(IllegalArgumentException::class.java) {
            AndroidPluginCatalogStore(catalogRoot).readVerifiedSnapshot()
        }
    }

    @Test
    fun commitRefusesCatalogRecordWithoutInstalledObject() {
        val fixture = TestPluginArchiveFactory.create(temporaryFolder, "missing.glplugin.zip")
        val snapshot = PluginCatalogSnapshot.empty().activate(record(fixture, apkVersionCode = 9))

        assertThrows(IllegalArgumentException::class.java) {
            AndroidPluginCatalogStore(temporaryFolder.newFolder("missing-catalog")).commit(snapshot)
        }
    }

    private fun record(fixture: TestPluginArchive, apkVersionCode: Long): PluginCatalogRecord =
        PluginCatalogRecord(
            manifest = fixture.manifest,
            pluginContentSha256 = fixture.contentRoot,
            provenance = PluginCatalogProvenance(
                kind = PluginCatalogProvenanceKind.BUILTIN_SEED,
                apkVersionCode = apkVersionCode,
                indexSha256 = "a".repeat(64),
            ),
            enabled = true,
            removable = false,
        )
}

internal data class TestPluginArchive(
    val archive: File,
    val manifest: PluginPackageManifest,
    val contentRoot: com.ghostlock.plugin.core.PluginContentSha256,
)

internal object TestPluginArchiveFactory {
    fun create(
        temporaryFolder: TemporaryFolder,
        name: String,
        versionCode: Long = 1,
        packageId: String = "com.ghostlock.diagnose",
        method: Int = ZipEntry.DEFLATED,
        timestamp: Long = 0L,
        reverseOrder: Boolean = false,
        className: String? = null,
    ): TestPluginArchive {
        val payloads = linkedMapOf(
            "code/classes.dex" to dex(className),
            "licenses/NOTICE.txt" to "GhostLock Diagnose notice\n".encodeToByteArray(),
        )
        val manifest = PluginPackageManifest(
            formatMajor = 1,
            formatMinor = 0,
            publisherId = "com.ghostlock.firstparty",
            packageId = packageId,
            versionCode = versionCode,
            versionName = "Diagnose $versionCode",
            variant = "android-arm64-v8a",
            minAndroidSdk = 31,
            hostApiMajor = 1,
            minHostApiMinor = 0,
            files = payloads.map { (path, bytes) -> PluginFileEntry(path, sha256(bytes)) },
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
                    executionDomains = listOf(
                        PluginExecutionDomain.APP,
                        PluginExecutionDomain.SHIZUKU_SHELL,
                    ),
                    runCapability = PluginRunCapability.DIAGNOSTIC_ONLY,
                ),
            ),
        ).normalized()
        val archive = temporaryFolder.newFile(name)
        ZipOutputStream(FileOutputStream(archive)).use { zip ->
            zip.setComment(if (reverseOrder) "repacked" else "original")
            val entries = payloads.entries.toList().let { if (reverseOrder) it.reversed() else it }
            putEntry(
                zip,
                "manifest.json",
                PluginMetadataCodec.encodeManifest(manifest).encodeToByteArray(),
                method,
                timestamp,
            )
            entries.forEach { (path, bytes) -> putEntry(zip, path, bytes, method, timestamp) }
        }
        return TestPluginArchive(
            archive = archive,
            manifest = manifest,
            contentRoot = PluginMetadataCodec.pluginContentSha256(manifest),
        )
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

    private fun dex(className: String?): ByteArray {
        val descriptors = listOfNotNull(className?.let { "L${it.replace('.', '/')};" })
        val count = descriptors.size
        val stringIdsOffset = DEX_HEADER_SIZE
        val typeIdsOffset = stringIdsOffset + count * 4
        val classDefsOffset = typeIdsOffset + count * 4
        val dataOffset = classDefsOffset + count * 32
        val encodedStrings = descriptors.map { descriptor ->
            byteArrayOf(descriptor.length.toByte()) + descriptor.encodeToByteArray() + byteArrayOf(0)
        }
        val output = ByteArray(dataOffset + encodedStrings.sumOf(ByteArray::size))
        "dex\n035\u0000".encodeToByteArray().copyInto(output)
        output.writeUInt32LeForFixture(32, output.size.toLong())
        output.writeUInt32LeForFixture(36, DEX_HEADER_SIZE.toLong())
        output.writeUInt32LeForFixture(40, 0x12345678)
        output.writeUInt32LeForFixture(56, count.toLong())
        output.writeUInt32LeForFixture(60, if (count == 0) 0 else stringIdsOffset.toLong())
        output.writeUInt32LeForFixture(64, count.toLong())
        output.writeUInt32LeForFixture(68, if (count == 0) 0 else typeIdsOffset.toLong())
        output.writeUInt32LeForFixture(96, count.toLong())
        output.writeUInt32LeForFixture(100, if (count == 0) 0 else classDefsOffset.toLong())
        output.writeUInt32LeForFixture(104, (output.size - dataOffset).toLong())
        output.writeUInt32LeForFixture(108, if (count == 0) 0 else dataOffset.toLong())
        var stringOffset = dataOffset
        descriptors.indices.forEach { ordinal ->
            output.writeUInt32LeForFixture(stringIdsOffset + ordinal * 4, stringOffset.toLong())
            output.writeUInt32LeForFixture(typeIdsOffset + ordinal * 4, ordinal.toLong())
            output.writeUInt32LeForFixture(classDefsOffset + ordinal * 32, ordinal.toLong())
            encodedStrings[ordinal].copyInto(output, stringOffset)
            stringOffset += encodedStrings[ordinal].size
        }
        MessageDigest.getInstance("SHA-1").digest(output.copyOfRange(32, output.size)).copyInto(output, 12)
        output.writeUInt32LeForFixture(
            8,
            Adler32().apply { update(output, 12, output.size - 12) }.value,
        )
        return output
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xff) }

    private const val DEX_HEADER_SIZE = 0x70
}

private fun ByteArray.writeUInt32LeForFixture(offset: Int, value: Long) {
    repeat(4) { byte -> this[offset + byte] = (value ushr (byte * 8)).toByte() }
}
