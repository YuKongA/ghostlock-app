package com.ghostlock.app.data.plugin

import com.ghostlock.plugin.core.PluginContentSha256
import com.ghostlock.plugin.core.PluginMetadataCodec
import com.ghostlock.plugin.core.PluginNativeLibrary
import com.ghostlock.plugin.core.PluginPackageManifest
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.LinkOption
import java.security.MessageDigest
import java.util.zip.Adler32
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import org.apache.commons.compress.archivers.zip.ZipArchiveEntry
import org.apache.commons.compress.archivers.zip.ZipFile as CommonsZipFile

/** Immutable result shared by builtin admission and RunHost materialization. */
internal data class VerifiedPluginArchive(
    val manifest: PluginPackageManifest,
    val pluginContentSha256: PluginContentSha256,
    val dexPaths: List<String>,
    val nativeLibraryPaths: List<String>,
    val definedClasses: Set<String>,
)

/**
 * Verifies one ordinary ZIP/ZIP64 package without treating ZIP bytes as plugin identity.
 *
 * Entry bodies are read through a fixed buffer. DEX and ELF entries are copied to caller-owned
 * scratch files only for random-access structural inspection; declared ZIP sizes never size an
 * allocation.
 */
internal class AndroidPluginArchiveVerifier {
    fun verify(
        archive: File,
        scratchDirectory: File,
        expectedContentRoot: PluginContentSha256? = null,
    ): VerifiedPluginArchive {
        require(Files.isRegularFile(archive.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            "plugin archive must be a regular file"
        }
        require(scratchDirectory.isDirectory) { "plugin verifier scratch directory is missing" }
        val layout = ZipLayout.read(archive)
        // Android's boot class path exposes an older Commons Compress ZipFile. Keep this call on
        // the long-standing constructor surface; ZipLayout and verifyPhysicalLayout below enforce
        // the single-disk and local-header invariants that the newer builder options would add.
        @Suppress("DEPRECATION")
        CommonsZipFile(archive).use { zip ->
                val entries = zip.entries.toList()
                require(entries.isNotEmpty()) { "plugin ZIP is empty" }
                verifyPhysicalLayout(archive, zip.entriesInPhysicalOrder.toList(), layout.centralDirectoryOffset)

                val regularByPath = linkedMapOf<String, ZipArchiveEntry>()
                entries.forEach { entry ->
                    val normalizedName = validateEntry(entry, zip)
                    if (!entry.isDirectory) {
                        require(regularByPath.put(normalizedName, entry) == null) {
                            "duplicate regular ZIP path: $normalizedName"
                        }
                    }
                }
                val manifestEntry = requireNotNull(regularByPath[MANIFEST_PATH]) {
                    "plugin ZIP is missing $MANIFEST_PATH"
                }
                val manifestBytes = ByteArrayOutputStream().also { output ->
                    readAndVerifyEntry(zip, manifestEntry, output::write)
                }.toByteArray()
                val manifest = PluginMetadataCodec.decodeManifest(decodeUtf8(manifestBytes))
                val expectedPaths = manifest.files.mapTo(mutableSetOf()) { it.path }.also {
                    it += MANIFEST_PATH
                }
                require(regularByPath.keys == expectedPaths) {
                    "ZIP regular files do not match manifest files"
                }

                val dexPaths = manifest.files.map { it.path }
                    .filter(DEX_PATH_PATTERN::matches)
                    .sortedBy(::dexOrdinal)
                val nativeByPath = manifest.nativeLibraries.associateBy(PluginNativeLibrary::path)
                val seenClasses = mutableSetOf<String>()
                manifest.files.forEach { declared ->
                    val entry = requireNotNull(regularByPath[declared.path])
                    val needsScratch = declared.path in dexPaths || declared.path in nativeByPath
                    val scratch = if (needsScratch) {
                        File.createTempFile("glplugin-verify-", ".entry", scratchDirectory)
                    } else {
                        null
                    }
                    try {
                        val digest = MessageDigest.getInstance("SHA-256")
                        if (scratch == null) {
                            readAndVerifyEntry(zip, entry) { bytes, offset, length ->
                                digest.update(bytes, offset, length)
                            }
                        } else {
                            FileOutputStream(scratch).use { output ->
                                readAndVerifyEntry(zip, entry) { bytes, offset, length ->
                                    digest.update(bytes, offset, length)
                                    output.write(bytes, offset, length)
                                }
                                output.fd.sync()
                            }
                        }
                        require(digest.digest().toLowerHex() == declared.sha256) {
                            "SHA-256 mismatch for ${declared.path}"
                        }
                        when {
                            declared.path in dexPaths -> {
                                DexInspector(requireNotNull(scratch)).classNames().forEach { className ->
                                    require(PROTECTED_CLASS_PREFIXES.none(className::startsWith)) {
                                        "plugin DEX defines protected class $className"
                                    }
                                    require(seenClasses.add(className)) {
                                        "duplicate plugin class $className"
                                    }
                                }
                            }
                            declared.path in nativeByPath -> {
                                val facts = ElfInspector(requireNotNull(scratch)).inspect()
                                verifyElfFacts(
                                    facts = facts,
                                    declaration = requireNotNull(nativeByPath[declared.path]),
                                    manifest = manifest,
                                )
                            }
                        }
                    } finally {
                        if (scratch != null && scratch.exists()) {
                            require(scratch.delete()) { "failed to delete plugin verifier scratch file" }
                        }
                    }
                }

                val contentRoot = PluginMetadataCodec.pluginContentSha256(manifest)
                require(expectedContentRoot == null || expectedContentRoot == contentRoot) {
                    "plugin semantic content root mismatch"
                }
                return VerifiedPluginArchive(
                    manifest = manifest,
                    pluginContentSha256 = contentRoot,
                    dexPaths = dexPaths,
                    nativeLibraryPaths = nativeByPath.keys.sorted(),
                    definedClasses = seenClasses.toSortedSet(),
                )
            }
    }

    private fun validateEntry(entry: ZipArchiveEntry, zip: CommonsZipFile): String {
        require(!entry.generalPurposeBit.usesEncryption()) { "encrypted ZIP entry is not supported" }
        require(!entry.generalPurposeBit.usesStrongEncryption()) {
            "strongly encrypted ZIP entry is not supported"
        }
        require(entry.method == ZipEntry.STORED || entry.method == ZipEntry.DEFLATED) {
            "unsupported ZIP method for ${entry.name}"
        }
        require(zip.canReadEntryData(entry)) { "unreadable ZIP entry ${entry.name}" }
        require(entry.size >= 0L && entry.compressedSize >= 0L && entry.crc >= 0L) {
            "ZIP entry metadata is incomplete for ${entry.name}"
        }
        require(!entry.isUnixSymlink) { "ZIP symlink is not allowed: ${entry.name}" }
        val type = entry.unixMode and UNIX_FILE_TYPE_MASK
        if (entry.isDirectory) {
            require(type == 0 || type == UNIX_DIRECTORY) {
                "special ZIP directory entry is not allowed: ${entry.name}"
            }
        } else {
            require(type == 0 || type == UNIX_REGULAR_FILE) {
                "special ZIP file entry is not allowed: ${entry.name}"
            }
        }
        return requireSafeZipPath(entry.name, entry.isDirectory)
    }

    private fun readAndVerifyEntry(
        zip: CommonsZipFile,
        entry: ZipArchiveEntry,
        consume: (ByteArray, Int, Int) -> Unit,
    ) {
        val crc = CRC32()
        var actualSize = 0L
        val buffer = ByteArray(STREAM_BUFFER_BYTES)
        zip.getInputStream(entry).use { input ->
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (count == 0) continue
                crc.update(buffer, 0, count)
                consume(buffer, 0, count)
                actualSize += count.toLong()
            }
        }
        require(actualSize == entry.size) { "uncompressed size mismatch for ${entry.name}" }
        require(crc.value == entry.crc) { "CRC mismatch for ${entry.name}" }
    }

    private fun verifyElfFacts(
        facts: ElfFacts,
        declaration: PluginNativeLibrary,
        manifest: PluginPackageManifest,
    ) {
        require(facts.elfClass == declaration.elfClass) {
            "ELF class mismatch for ${declaration.path}"
        }
        require(facts.machine == declaration.elfMachine) {
            "ELF machine mismatch for ${declaration.path}"
        }
        require(facts.loadSegmentsAre16KiBAligned) {
            "ELF PT_LOAD alignment is below 16 KiB for ${declaration.path}"
        }
        require(facts.soname == declaration.soname) {
            "ELF SONAME mismatch for ${declaration.path}"
        }
        require(facts.needed.sorted() == declaration.neededLibraries.sorted()) {
            "ELF DT_NEEDED mismatch for ${declaration.path}"
        }
        val packageSonames = manifest.nativeLibraries.mapNotNullTo(mutableSetOf()) { it.soname }
        facts.needed.forEach { needed ->
            require(needed in PLATFORM_NEEDED || needed in packageSonames) {
                "ELF dependency $needed is neither platform-owned nor package-private"
            }
        }
    }

    private companion object {
        const val MANIFEST_PATH = "manifest.json"
        const val STREAM_BUFFER_BYTES = 16 * 1024
        const val UNIX_FILE_TYPE_MASK = 0xf000
        const val UNIX_DIRECTORY = 0x4000
        const val UNIX_REGULAR_FILE = 0x8000
        val DEX_PATH_PATTERN = Regex("code/classes(?:[2-9][0-9]*)?\\.dex")
        val PROTECTED_CLASS_PREFIXES = listOf(
            "com.ghostlock.plugin.api.v1.",
            "com.ghostlock.app.",
            "java.",
            "javax.",
            "android.",
            "dalvik.",
            "libcore.",
        )
        val PLATFORM_NEEDED = setOf("libc.so", "libdl.so", "liblog.so", "libm.so")
    }
}

private data class ZipLayout(val centralDirectoryOffset: Long) {
    companion object {
        fun read(file: File): ZipLayout = RandomAccessFile(file, "r").use { input ->
            val length = input.length()
            require(length >= EOCD_MIN_BYTES) { "ZIP is too short" }
            val searchLength = minOf(length, EOCD_MAX_SEARCH_BYTES.toLong()).toInt()
            val searchStart = length - searchLength
            val tail = ByteArray(searchLength)
            input.seek(searchStart)
            input.readFully(tail)
            var eocdOffset = -1
            for (offset in tail.size - EOCD_MIN_BYTES downTo 0) {
                if (tail.readUInt32Le(offset) != EOCD_SIGNATURE) continue
                val commentLength = tail.readUInt16Le(offset + 20)
                if (searchStart + offset + EOCD_MIN_BYTES + commentLength == length) {
                    eocdOffset = offset
                    break
                }
            }
            require(eocdOffset >= 0) { "ZIP EOCD is missing or not at EOF" }
            val disk = tail.readUInt16Le(eocdOffset + 4)
            val centralDisk = tail.readUInt16Le(eocdOffset + 6)
            require(disk == 0 && centralDisk == 0) { "multi-disk ZIP is not supported" }
            val centralOffset32 = tail.readUInt32Le(eocdOffset + 16)
            val centralOffset = if (centralOffset32 != UINT32_MAX) {
                centralOffset32
            } else {
                val absoluteEocd = searchStart + eocdOffset
                require(absoluteEocd >= ZIP64_LOCATOR_BYTES) { "ZIP64 locator is missing" }
                input.seek(absoluteEocd - ZIP64_LOCATOR_BYTES)
                require(input.readUInt32Le() == ZIP64_LOCATOR_SIGNATURE) { "ZIP64 locator is invalid" }
                require(input.readUInt32Le() == 0L) { "multi-disk ZIP64 is not supported" }
                val zip64Offset = input.readUInt64Le()
                require(input.readUInt32Le() == 1L) { "multi-disk ZIP64 is not supported" }
                require(zip64Offset >= 0L && zip64Offset + ZIP64_EOCD_MIN_BYTES <= length) {
                    "ZIP64 EOCD offset is invalid"
                }
                input.seek(zip64Offset)
                require(input.readUInt32Le() == ZIP64_EOCD_SIGNATURE) { "ZIP64 EOCD is invalid" }
                input.seek(zip64Offset + 16)
                require(input.readUInt32Le() == 0L && input.readUInt32Le() == 0L) {
                    "multi-disk ZIP64 is not supported"
                }
                input.seek(zip64Offset + 48)
                input.readUInt64Le()
            }
            require(centralOffset in 1 until (searchStart + eocdOffset)) {
                "ZIP central directory offset is invalid"
            }
            ZipLayout(centralOffset)
        }

        private const val EOCD_MIN_BYTES = 22
        private const val EOCD_MAX_SEARCH_BYTES = EOCD_MIN_BYTES + 0xffff
        private const val ZIP64_LOCATOR_BYTES = 20L
        private const val ZIP64_EOCD_MIN_BYTES = 56L
        private const val EOCD_SIGNATURE = 0x06054b50L
        private const val ZIP64_LOCATOR_SIGNATURE = 0x07064b50L
        private const val ZIP64_EOCD_SIGNATURE = 0x06064b50L
        private const val UINT32_MAX = 0xffffffffL
    }
}

private fun verifyPhysicalLayout(
    archive: File,
    entries: List<ZipArchiveEntry>,
    centralDirectoryOffset: Long,
) {
    require(entries.isNotEmpty()) { "ZIP physical entry list is empty" }
    RandomAccessFile(archive, "r").use { input ->
        var localHeaderOffset = 0L
        entries.forEachIndexed { index, entry ->
            require(localHeaderOffset < centralDirectoryOffset) { "ZIP local header order is invalid" }
            input.seek(localHeaderOffset)
            require(input.readUInt32Le() == LOCAL_FILE_HEADER_SIGNATURE) {
                "ZIP local header signature is invalid for ${entry.name}"
            }
            input.skipBytes(2)
            val localFlags = input.readUInt16Le()
            val localMethod = input.readUInt16Le()
            input.skipBytes(4)
            val localCrc = input.readUInt32Le()
            val localCompressedSize = input.readUInt32Le()
            val localSize = input.readUInt32Le()
            val nameLength = input.readUInt16Le()
            val extraLength = input.readUInt16Le()
            val rawName = ByteArray(nameLength)
            input.readFully(rawName)
            require(rawName.contentEquals(entry.rawName)) {
                "ZIP local/central name mismatch for ${entry.name}"
            }
            require(localFlags == entry.rawFlag) { "ZIP local/central flags mismatch for ${entry.name}" }
            require(localMethod == entry.method) { "ZIP local/central method mismatch for ${entry.name}" }
            val dataOffset = localHeaderOffset + LOCAL_FILE_HEADER_BYTES + nameLength + extraLength
            require(dataOffset <= centralDirectoryOffset && entry.compressedSize <= centralDirectoryOffset - dataOffset) {
                "ZIP entry overlaps the next structure"
            }
            val dataEnd = dataOffset + entry.compressedSize
            val nextOffset = if (!entry.generalPurposeBit.usesDataDescriptor()) {
                require(localCrc == entry.crc) { "ZIP local/central CRC mismatch for ${entry.name}" }
                require(localCompressedSize == entry.compressedSize || localCompressedSize == UINT32_MAX) {
                    "ZIP local/central compressed size mismatch for ${entry.name}"
                }
                require(localSize == entry.size || localSize == UINT32_MAX) {
                    "ZIP local/central size mismatch for ${entry.name}"
                }
                dataEnd
            } else {
                dataEnd + verifyDataDescriptor(
                    input = input,
                    offset = dataEnd,
                    entry = entry,
                    zip64 = localCompressedSize == UINT32_MAX || localSize == UINT32_MAX ||
                        entry.compressedSize > UINT32_MAX || entry.size > UINT32_MAX,
                    expectedNextSignature = if (index == entries.lastIndex) {
                        CENTRAL_DIRECTORY_HEADER_SIGNATURE
                    } else {
                        LOCAL_FILE_HEADER_SIGNATURE
                    },
                    centralDirectoryOffset = centralDirectoryOffset,
                )
            }
            val expectedSignature = if (index == entries.lastIndex) {
                CENTRAL_DIRECTORY_HEADER_SIGNATURE
            } else {
                LOCAL_FILE_HEADER_SIGNATURE
            }
            require(nextOffset <= centralDirectoryOffset) {
                "ZIP entry ends outside the physical file area: ${entry.name}"
            }
            input.seek(nextOffset)
            require(input.readUInt32Le() == expectedSignature) {
                "unexpected bytes after ZIP entry ${entry.name}"
            }
            localHeaderOffset = nextOffset
        }
        require(localHeaderOffset == centralDirectoryOffset) {
            "ZIP physical entries do not end at the central directory"
        }
    }
}

private fun verifyDataDescriptor(
    input: RandomAccessFile,
    offset: Long,
    entry: ZipArchiveEntry,
    zip64: Boolean,
    expectedNextSignature: Long,
    centralDirectoryOffset: Long,
): Long {
    val valueBytes = if (zip64) 20L else 12L
    val signedBytes = valueBytes + 4L
    fun matches(length: Long, signed: Boolean): Boolean {
        if (offset > centralDirectoryOffset - length) return false
        input.seek(offset)
        if (signed && input.readUInt32Le() != DATA_DESCRIPTOR_SIGNATURE) return false
        val crc = input.readUInt32Le()
        val compressed = if (zip64) input.readUInt64Le() else input.readUInt32Le()
        val size = if (zip64) input.readUInt64Le() else input.readUInt32Le()
        if (crc != entry.crc || compressed != entry.compressedSize || size != entry.size) return false
        input.seek(offset + length)
        return input.readUInt32Le() == expectedNextSignature
    }
    val matches = buildList {
        if (matches(valueBytes, false)) add(valueBytes)
        if (matches(signedBytes, true)) add(signedBytes)
    }
    require(matches.size == 1) {
        "invalid or ambiguous ZIP data descriptor for ${entry.name}"
    }
    return matches.single()
}

private class DexInspector(private val file: File) {
    fun classNames(): Set<String> = RandomAccessFile(file, "r").use { input ->
        require(input.length() >= DEX_HEADER_BYTES) { "DEX header is truncated" }
        val magic = ByteArray(8)
        input.readFully(magic)
        require(magic.copyOfRange(0, 4).contentEquals(byteArrayOf('d'.code.toByte(), 'e'.code.toByte(), 'x'.code.toByte(), '\n'.code.toByte()))) {
            "DEX magic is invalid"
        }
        require(magic[7] == 0.toByte()) { "DEX version terminator is invalid" }
        require(magic.copyOfRange(4, 7).all { it.toInt().toChar().isDigit() }) {
            "DEX version is invalid"
        }
        val expectedChecksum = input.readUInt32Le()
        val expectedSignature = ByteArray(20).also(input::readFully)
        val fileSize = input.readUInt32Le()
        val headerSize = input.readUInt32Le()
        val endianTag = input.readUInt32Le()
        require(fileSize == input.length()) { "DEX file size mismatch" }
        require(headerSize == DEX_HEADER_BYTES) { "DEX header size is invalid" }
        require(endianTag == DEX_ENDIAN_CONSTANT) { "DEX endian tag is invalid" }
        verifyDexDigests(input, expectedChecksum, expectedSignature)

        val stringCount = input.readUInt32LeAt(56)
        val stringOffset = input.readUInt32LeAt(60)
        val typeCount = input.readUInt32LeAt(64)
        val typeOffset = input.readUInt32LeAt(68)
        val classCount = input.readUInt32LeAt(96)
        val classOffset = input.readUInt32LeAt(100)
        requireTable(input.length(), stringOffset, stringCount, 4, "DEX string_ids")
        requireTable(input.length(), typeOffset, typeCount, 4, "DEX type_ids")
        requireTable(input.length(), classOffset, classCount, 32, "DEX class_defs")

        buildSet {
            repeat(classCount.toIntExact("DEX class count")) { classOrdinal ->
                val classIndex = input.readUInt32LeAt(classOffset + classOrdinal * 32L)
                require(classIndex < typeCount) { "DEX class type index is out of range" }
                val descriptorIndex = input.readUInt32LeAt(typeOffset + classIndex * 4L)
                require(descriptorIndex < stringCount) { "DEX descriptor index is out of range" }
                val stringDataOffset = input.readUInt32LeAt(stringOffset + descriptorIndex * 4L)
                require(stringDataOffset in 1 until input.length()) { "DEX string data offset is invalid" }
                val descriptor = input.readMutf8At(stringDataOffset)
                require(descriptor.startsWith('L') && descriptor.endsWith(';')) {
                    "DEX class descriptor is invalid"
                }
                val binaryName = descriptor.substring(1, descriptor.length - 1).replace('/', '.')
                require(binaryName.isNotBlank() && '\u0000' !in binaryName) {
                    "DEX class binary name is invalid"
                }
                require(add(binaryName)) { "duplicate plugin class $binaryName" }
            }
        }
    }

    private fun verifyDexDigests(
        input: RandomAccessFile,
        expectedChecksum: Long,
        expectedSignature: ByteArray,
    ) {
        val sha1 = MessageDigest.getInstance("SHA-1")
        val adler = Adler32()
        val buffer = ByteArray(16 * 1024)
        input.seek(12)
        var position = 12L
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            if (position < 32L) {
                adler.update(buffer, 0, count)
                val signatureStart = (32L - position).coerceAtLeast(0L).toInt().coerceAtMost(count)
                if (signatureStart < count) sha1.update(buffer, signatureStart, count - signatureStart)
            } else {
                adler.update(buffer, 0, count)
                sha1.update(buffer, 0, count)
            }
            position += count
        }
        require(adler.value == expectedChecksum) { "DEX Adler-32 mismatch" }
        require(sha1.digest().contentEquals(expectedSignature)) { "DEX SHA-1 signature mismatch" }
    }

    private companion object {
        const val DEX_HEADER_BYTES = 0x70L
        const val DEX_ENDIAN_CONSTANT = 0x12345678L
    }
}

private data class ElfFacts(
    val elfClass: Int,
    val machine: Int,
    val soname: String?,
    val needed: List<String>,
    val loadSegmentsAre16KiBAligned: Boolean,
)

private class ElfInspector(private val file: File) {
    fun inspect(): ElfFacts = RandomAccessFile(file, "r").use { input ->
        require(input.length() >= ELF64_HEADER_BYTES) { "ELF header is truncated" }
        val ident = ByteArray(16).also(input::readFully)
        require(ident.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 'E'.code.toByte(), 'L'.code.toByte(), 'F'.code.toByte()))) {
            "ELF magic is invalid"
        }
        val elfClass = ident[4].toInt() and 0xff
        require(elfClass == ELFCLASS64) { "only ELF64 is supported in B1" }
        require((ident[5].toInt() and 0xff) == ELFDATA2LSB) { "ELF must be little-endian" }
        val machine = input.readUInt16LeAt(18)
        val programOffset = input.readUInt64LeAt(32)
        val programEntrySize = input.readUInt16LeAt(54)
        val programCount = input.readUInt16LeAt(56)
        require(programEntrySize >= ELF64_PROGRAM_HEADER_BYTES) { "ELF program header size is invalid" }
        requireTable(input.length(), programOffset, programCount.toLong(), programEntrySize, "ELF program headers")

        val loads = mutableListOf<ElfLoadSegment>()
        var dynamic: ElfDynamicSegment? = null
        repeat(programCount) { ordinal ->
            val offset = programOffset + ordinal.toLong() * programEntrySize
            val type = input.readUInt32LeAt(offset)
            val fileOffset = input.readUInt64LeAt(offset + 8)
            val virtualAddress = input.readUInt64LeAt(offset + 16)
            val fileSize = input.readUInt64LeAt(offset + 32)
            val alignment = input.readUInt64LeAt(offset + 48)
            require(fileOffset <= input.length() && fileSize <= input.length() - fileOffset) {
                "ELF segment range is invalid"
            }
            when (type) {
                PT_LOAD -> loads += ElfLoadSegment(fileOffset, virtualAddress, fileSize, alignment)
                PT_DYNAMIC -> dynamic = ElfDynamicSegment(fileOffset, fileSize)
            }
        }
        require(loads.isNotEmpty()) { "ELF has no PT_LOAD segment" }
        val aligned = loads.all { segment ->
            segment.alignment >= PAGE_16K && segment.alignment.isPowerOfTwo() &&
                (segment.virtualAddress - segment.fileOffset).floorMod(PAGE_16K) == 0L
        }
        val dynamicFacts = dynamic?.let { readDynamic(input, it, loads) } ?: DynamicFacts(null, emptyList())
        ElfFacts(
            elfClass = elfClass,
            machine = machine,
            soname = dynamicFacts.soname,
            needed = dynamicFacts.needed,
            loadSegmentsAre16KiBAligned = aligned,
        )
    }

    private fun readDynamic(
        input: RandomAccessFile,
        dynamic: ElfDynamicSegment,
        loads: List<ElfLoadSegment>,
    ): DynamicFacts {
        require(dynamic.fileSize % ELF64_DYNAMIC_ENTRY_BYTES == 0L) { "ELF PT_DYNAMIC size is invalid" }
        var stringTableAddress: Long? = null
        var stringTableSize: Long? = null
        var sonameOffset: Long? = null
        val neededOffsets = mutableListOf<Long>()
        val entryCount = dynamic.fileSize / ELF64_DYNAMIC_ENTRY_BYTES
        for (ordinal in 0 until entryCount) {
            val offset = dynamic.fileOffset + ordinal * ELF64_DYNAMIC_ENTRY_BYTES
            when (input.readInt64LeAt(offset)) {
                DT_NULL -> break
                DT_NEEDED -> neededOffsets += input.readUInt64LeAt(offset + 8)
                DT_STRTAB -> stringTableAddress = input.readUInt64LeAt(offset + 8)
                DT_STRSZ -> stringTableSize = input.readUInt64LeAt(offset + 8)
                DT_SONAME -> sonameOffset = input.readUInt64LeAt(offset + 8)
                else -> Unit
            }
        }
        if (neededOffsets.isEmpty() && sonameOffset == null) return DynamicFacts(null, emptyList())
        val tableAddress = requireNotNull(stringTableAddress) { "ELF dynamic string table is missing" }
        val tableSize = requireNotNull(stringTableSize) { "ELF dynamic string table size is missing" }
        require(tableSize > 0L) { "ELF dynamic string table is empty" }
        val tableOffset = loads.firstNotNullOfOrNull { load ->
            val relativeAddress = tableAddress - load.virtualAddress
            if (tableAddress >= load.virtualAddress &&
                relativeAddress <= load.fileSize &&
                tableSize <= load.fileSize - relativeAddress
            ) {
                load.fileOffset + relativeAddress
            } else {
                null
            }
        } ?: throw IllegalArgumentException("ELF dynamic string table is outside PT_LOAD")
        fun stringAt(relativeOffset: Long): String {
            require(relativeOffset in 0 until tableSize) { "ELF dynamic string offset is invalid" }
            return input.readAsciiZAt(tableOffset + relativeOffset, tableOffset + tableSize)
        }
        val needed = neededOffsets.map(::stringAt)
        require(needed.toSet().size == needed.size) { "duplicate ELF DT_NEEDED" }
        return DynamicFacts(sonameOffset?.let(::stringAt), needed)
    }

    private companion object {
        const val ELF64_HEADER_BYTES = 64L
        const val ELF64_PROGRAM_HEADER_BYTES = 56
        const val ELF64_DYNAMIC_ENTRY_BYTES = 16L
        const val ELFCLASS64 = 2
        const val ELFDATA2LSB = 1
        const val PT_LOAD = 1L
        const val PT_DYNAMIC = 2L
        const val DT_NULL = 0L
        const val DT_NEEDED = 1L
        const val DT_STRTAB = 5L
        const val DT_STRSZ = 10L
        const val DT_SONAME = 14L
        const val PAGE_16K = 16L * 1024L
    }
}

private data class ElfLoadSegment(
    val fileOffset: Long,
    val virtualAddress: Long,
    val fileSize: Long,
    val alignment: Long,
)

private data class ElfDynamicSegment(val fileOffset: Long, val fileSize: Long)

private data class DynamicFacts(val soname: String?, val needed: List<String>)

private fun requireSafeZipPath(raw: String, directory: Boolean): String {
    require(raw.isNotEmpty()) { "ZIP path is empty" }
    require(!raw.startsWith('/')) { "absolute ZIP path is not allowed: $raw" }
    require('\\' !in raw) { "backslash ZIP path is not allowed: $raw" }
    val normalized = if (directory) raw.removeSuffix("/") else raw
    require(normalized.isNotEmpty()) { "root ZIP directory entry is not allowed" }
    require(normalized.encodeToByteArray().size <= 240) { "ZIP path exceeds 240 UTF-8 bytes" }
    require(normalized.split('/').none { it.isEmpty() || it == "." || it == ".." }) {
        "unsafe ZIP path segment: $raw"
    }
    return normalized
}

private fun decodeUtf8(bytes: ByteArray): String = StandardCharsets.UTF_8.newDecoder()
    .onMalformedInput(CodingErrorAction.REPORT)
    .onUnmappableCharacter(CodingErrorAction.REPORT)
    .decode(ByteBuffer.wrap(bytes))
    .toString()

private fun dexOrdinal(path: String): Int = if (path == "code/classes.dex") {
    1
} else {
    path.removePrefix("code/classes").removeSuffix(".dex").toInt()
}

private fun RandomAccessFile.readUInt16Le(): Int {
    val first = read()
    val second = read()
    require(first >= 0 && second >= 0) { "unexpected EOF" }
    return first or (second shl 8)
}

private fun RandomAccessFile.readUInt32Le(): Long {
    var value = 0L
    repeat(4) { byte ->
        val next = read()
        require(next >= 0) { "unexpected EOF" }
        value = value or (next.toLong() shl (byte * 8))
    }
    return value
}

private fun RandomAccessFile.readUInt64Le(): Long {
    var value = 0L
    repeat(8) { byte ->
        val next = read()
        require(next >= 0) { "unexpected EOF" }
        value = value or (next.toLong() shl (byte * 8))
    }
    require(value >= 0L) { "unsigned 64-bit value exceeds supported file range" }
    return value
}

private fun RandomAccessFile.readUInt16LeAt(offset: Long): Int {
    seek(offset)
    return readUInt16Le()
}

private fun RandomAccessFile.readUInt32LeAt(offset: Long): Long {
    seek(offset)
    return readUInt32Le()
}

private fun RandomAccessFile.readUInt64LeAt(offset: Long): Long {
    seek(offset)
    return readUInt64Le()
}

private fun RandomAccessFile.readInt64LeAt(offset: Long): Long {
    seek(offset)
    var value = 0L
    repeat(8) { byte ->
        val next = read()
        require(next >= 0) { "unexpected EOF" }
        value = value or (next.toLong() shl (byte * 8))
    }
    return value
}

private fun RandomAccessFile.readMutf8At(offset: Long): String {
    seek(offset)
    readUleb128()
    val output = StringBuilder()
    while (true) {
        val first = read()
        require(first >= 0) { "unterminated DEX MUTF-8 string" }
        if (first == 0) return output.toString()
        when {
            first and 0x80 == 0 -> output.append(first.toChar())
            first and 0xe0 == 0xc0 -> {
                val second = readContinuationByte()
                output.append((((first and 0x1f) shl 6) or second).toChar())
            }
            first and 0xf0 == 0xe0 -> {
                val second = readContinuationByte()
                val third = readContinuationByte()
                output.append((((first and 0x0f) shl 12) or (second shl 6) or third).toChar())
            }
            else -> throw IllegalArgumentException("invalid DEX MUTF-8 byte")
        }
    }
}

private fun RandomAccessFile.readContinuationByte(): Int {
    val value = read()
    require(value >= 0) { "truncated DEX MUTF-8 continuation byte" }
    require(value and 0xc0 == 0x80) { "invalid DEX MUTF-8 continuation byte" }
    return value and 0x3f
}

private fun RandomAccessFile.readUleb128(): Long {
    var value = 0L
    var shift = 0
    repeat(5) {
        val byte = read()
        require(byte >= 0) { "truncated ULEB128" }
        value = value or ((byte and 0x7f).toLong() shl shift)
        if (byte and 0x80 == 0) return value
        shift += 7
    }
    throw IllegalArgumentException("ULEB128 is too long")
}

private fun RandomAccessFile.readAsciiZAt(offset: Long, endExclusive: Long): String {
    seek(offset)
    val output = StringBuilder()
    while (filePointer < endExclusive) {
        val byte = read()
        require(byte >= 0) { "unterminated ELF string" }
        if (byte == 0) return output.toString()
        require(byte in 0x20..0x7e) { "ELF string is not printable ASCII" }
        output.append(byte.toChar())
    }
    throw IllegalArgumentException("unterminated ELF string")
}

private fun requireTable(
    fileLength: Long,
    offset: Long,
    count: Long,
    entrySize: Int,
    label: String,
) {
    require(offset >= 0L && count >= 0L && entrySize > 0) { "$label metadata is invalid" }
    require(count <= (Long.MAX_VALUE - offset) / entrySize) { "$label size overflows" }
    require(offset + count * entrySize <= fileLength) { "$label is outside the file" }
}

private fun Long.toIntExact(label: String): Int {
    require(this in 0..Int.MAX_VALUE.toLong()) { "$label exceeds supported iteration range" }
    return toInt()
}

private fun Long.isPowerOfTwo(): Boolean = this > 0L && this and (this - 1L) == 0L

private fun Long.floorMod(divisor: Long): Long = ((this % divisor) + divisor) % divisor

private fun ByteArray.readUInt16Le(offset: Int): Int =
    (this[offset].toInt() and 0xff) or ((this[offset + 1].toInt() and 0xff) shl 8)

private fun ByteArray.readUInt32Le(offset: Int): Long =
    (this[offset].toLong() and 0xff) or
        ((this[offset + 1].toLong() and 0xff) shl 8) or
        ((this[offset + 2].toLong() and 0xff) shl 16) or
        ((this[offset + 3].toLong() and 0xff) shl 24)

private fun ByteArray.toLowerHex(): String = buildString(size * 2) {
    this@toLowerHex.forEach { byte ->
        val value = byte.toInt() and 0xff
        append(HEX[value ushr 4])
        append(HEX[value and 0x0f])
    }
}

private fun java.util.Enumeration<ZipArchiveEntry>.toList(): List<ZipArchiveEntry> = buildList {
    while (hasMoreElements()) add(nextElement())
}

private const val HEX = "0123456789abcdef"
private const val UINT32_MAX = 0xffffffffL
private const val LOCAL_FILE_HEADER_SIGNATURE = 0x04034b50L
private const val CENTRAL_DIRECTORY_HEADER_SIGNATURE = 0x02014b50L
private const val DATA_DESCRIPTOR_SIGNATURE = 0x08074b50L
private const val LOCAL_FILE_HEADER_BYTES = 30L
