package com.ghostlock.app.data.ota

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.apache.commons.compress.compressors.bzip2.BZip2CompressorInputStream
import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.util.Locale
import kotlin.math.min

/**
 * High-performance streaming OTA partition extractor in pure Kotlin.
 * Downloads only the payload metadata and the blocks needed for boot and xbl_config.
 */
object OtaPayloadExtractor {

    data class ExtractedPartitions(
        val bootFile: File, val xblConfigFile: File? = null
    )

    private const val END_BYTES_SIZE = 65536
    private const val LOCAL_HEADER_PROBE_SIZE = 256

    suspend fun extractPartitions(
        url: String, workDir: File, onLog: (String) -> Unit
    ): ExtractedPartitions {
        val reader = HttpRangeReader()
        val fileLength = reader.fileLength(url) ?: throw IOException("Failed to connect or determine file length from URL")
        if (fileLength <= 0L) {
            throw IOException("Remote file length is zero or invalid")
        }

        onLog("locating payload metadata (file size: ${formatSize(fileLength)})...")

        // 1. Determine payload.bin start offset within the file
        val payloadStart = locatePayloadStart(reader, url, fileLength, onLog)

        // 2. Read payload header (24 bytes)
        val headerBytes =
            reader.read(url, payloadStart, PayloadBinUtils.HEADER_SIZE) ?: throw IOException("Failed to read payload.bin header at offset $payloadStart")
        val payloadHeader = PayloadBinUtils.parseHeader(headerBytes) ?: throw IOException("Invalid payload.bin header (missing CrAU magic)")

        // 3. Read manifest bytes
        val manifestStart = payloadStart + PayloadBinUtils.HEADER_SIZE
        if (payloadHeader.manifestSize > Int.MAX_VALUE) {
            throw IOException("Payload manifest size exceeds maximum supported memory (${payloadHeader.manifestSize} bytes)")
        }
        val manifest = reader.read(url, manifestStart, payloadHeader.manifestSize.toInt()) ?: throw IOException(
            "Failed to read payload manifest (${
                formatSize(payloadHeader.manifestSize)
            })"
        )

        // 4. Inspect available partitions
        val partitions = PayloadBinUtils.listPartitions(manifest)
        if ("boot" !in partitions) {
            throw IOException("Payload does not contain 'boot' partition (found: ${partitions.joinToString()})")
        }
        val want = mutableListOf("boot")
        if ("xbl_config" in partitions) {
            want.add("xbl_config")
        }
        onLog("analyzing partitions: ${want.joinToString(", ")}")

        val blockSize = PayloadBinUtils.blockSize(manifest)
        val dataBase = payloadStart + PayloadBinUtils.HEADER_SIZE + payloadHeader.manifestSize + payloadHeader.signatureSize

        // 5. Gather operations for the partitions
        val partitionOps = want.associateWith { name ->
            val ops = PayloadBinUtils.partitionOperations(manifest, name)
            if (ops.isEmpty()) throw IOException("Partition '$name' has no operations in payload")
            ops
        }

        val totalDownloadBytes = partitionOps.values.sumOf { ops -> ops.sumOf { it.dataLength } }
        var currentDownloadedBytes = 0L
        var lastLogTime = 0L

        fun reportProgress(force: Boolean = false) {
            val now = System.currentTimeMillis()
            if (force || now - lastLogTime >= 2000) {
                lastLogTime = now
                val percent = if (totalDownloadBytes > 0) {
                    currentDownloadedBytes.toDouble() / totalDownloadBytes * 100.0
                } else 0.0
                onLog(
                    "download: ${formatSize(currentDownloadedBytes)} / ${
                        formatSize(
                            totalDownloadBytes
                        )
                    } (" + String.format(Locale.US, "%.1f", percent) + "%)"
                )
            }
        }

        // 6. Extract partitions
        val timestamp = System.currentTimeMillis()
        var bootFile: File? = null
        var xblConfigFile: File? = null

        try {
            for ((name, ops) in partitionOps) {
                val outFile = File(workDir, "ghostlock_ota_${name}_$timestamp.img")
                if (name == "boot") bootFile = outFile else xblConfigFile = outFile

                val totalSize = ops.flatMap { it.destExtents }.maxOfOrNull { (it.startBlock + it.numBlocks) * blockSize }
                    ?: throw IOException("Partition '$name' has zero length")

                withContext(Dispatchers.IO) {
                    RandomAccessFile(outFile, "rw").use { raf ->
                        raf.setLength(totalSize)

                        for (op in ops) {
                            when (op.type) {
                                PayloadBinUtils.OP_ZERO, PayloadBinUtils.OP_DISCARD -> {
                                    // Already zero-filled by setLength
                                }

                                PayloadBinUtils.OP_REPLACE -> {
                                    val data = reader.read(
                                        url, dataBase + op.dataOffset, op.dataLength.toInt()
                                    ) ?: throw IOException("Failed to read REPLACE operation for $name at offset ${dataBase + op.dataOffset}")
                                    writeExtents(raf, data, op.destExtents, blockSize)
                                    currentDownloadedBytes += op.dataLength
                                    reportProgress()
                                }

                                PayloadBinUtils.OP_REPLACE_XZ -> {
                                    val compressed = reader.read(
                                        url, dataBase + op.dataOffset, op.dataLength.toInt()
                                    ) ?: throw IOException("Failed to read REPLACE_XZ operation for $name at offset ${dataBase + op.dataOffset}")
                                    val uncompressedSize = op.destExtents.sumOf { it.numBlocks } * blockSize
                                    val decompressed = XzDecoder.decode(compressed, uncompressedSize.toInt())
                                    writeExtents(raf, decompressed, op.destExtents, blockSize)
                                    currentDownloadedBytes += op.dataLength
                                    reportProgress()
                                }

                                PayloadBinUtils.OP_REPLACE_BZ -> {
                                    val compressed = reader.read(
                                        url, dataBase + op.dataOffset, op.dataLength.toInt()
                                    ) ?: throw IOException("Failed to read REPLACE_BZ operation for $name at offset ${dataBase + op.dataOffset}")
                                    val decompressed = BZip2CompressorInputStream(
                                        ByteArrayInputStream(compressed)
                                    ).use { it.readBytes() }
                                    writeExtents(raf, decompressed, op.destExtents, blockSize)
                                    currentDownloadedBytes += op.dataLength
                                    reportProgress()
                                }

                                else -> {
                                    throw IOException("Unsupported payload operation type: ${op.type} in partition '$name'")
                                }
                            }
                        }
                    }
                }
                onLog("extracted $name=${outFile.name} (${formatSize(outFile.length())}) from payload")
            }
            reportProgress(force = true)
            return ExtractedPartitions(bootFile!!, xblConfigFile)
        } catch (e: Exception) {
            bootFile?.delete()
            xblConfigFile?.delete()
            throw e
        }
    }

    private suspend fun locatePayloadStart(
        reader: HttpRangeReader, url: String, fileLength: Long, onLog: (String) -> Unit
    ): Long {
        // Probe head first: is it already a raw payload.bin?
        val headBytes = reader.read(url, 0L, 4)
        if (headBytes != null && headBytes.size >= 4 && headBytes.contentEquals(PayloadBinUtils.PAYLOAD_MAGIC)) {
            onLog("detected direct payload.bin")
            return 0L
        }

        // Locate via ZIP Central Directory
        val tailSize = min(fileLength, END_BYTES_SIZE.toLong()).toInt()
        val tailBytes = reader.read(url, fileLength - tailSize, tailSize) ?: throw IOException("Failed to read ZIP tail from remote file")

        val cd = ZipFileUtils.locateCentralDirectory(tailBytes, fileLength)
        if (cd.offset < 0 || cd.size <= 0 || cd.offset + cd.size > fileLength) {
            throw IOException("Failed to locate ZIP Central Directory in remote file")
        }

        val cdBytes = reader.read(url, cd.offset, cd.size.toInt()) ?: throw IOException("Failed to read ZIP Central Directory (${cd.size} bytes)")

        val entry = ZipFileUtils.locateEntries(
            cdBytes, setOf(PayloadBinUtils.PAYLOAD_ENTRY)
        )[PayloadBinUtils.PAYLOAD_ENTRY] ?: throw IOException("Remote OTA ZIP does not contain '${PayloadBinUtils.PAYLOAD_ENTRY}'")

        if (entry.method != 0) {
            throw IOException("payload.bin in remote ZIP must be Stored (uncompressed), found compression method ${entry.method}")
        }

        val headerOffset = entry.localHeaderOffset
        if (headerOffset !in 0..<fileLength) {
            throw IOException("Invalid local header offset in ZIP: $headerOffset")
        }

        val probeSize = min(fileLength - headerOffset, LOCAL_HEADER_PROBE_SIZE.toLong()).toInt()
        val localHeaderBytes = reader.read(url, headerOffset, probeSize) ?: throw IOException("Failed to read local file header for payload.bin")

        val internalOffset = ZipFileUtils.locateLocalFileOffset(localHeaderBytes)
        if (internalOffset !in 0..probeSize.toLong()) {
            throw IOException("Failed to parse local file header offset for payload.bin")
        }

        val payloadStart = headerOffset + internalOffset
        onLog("located payload.bin in OTA zip at offset $payloadStart (${formatSize(entry.uncompressedSize)})")
        return payloadStart
    }

    private fun writeExtents(
        raf: RandomAccessFile, data: ByteArray, extents: List<PayloadBinUtils.Extent>, blockSize: Long
    ) {
        var dataOffset = 0
        for (extent in extents) {
            val extentBytes = (extent.numBlocks * blockSize).toInt()
            val end = min(dataOffset + extentBytes, data.size)
            if (dataOffset < end) {
                raf.seek(extent.startBlock * blockSize)
                raf.write(data, dataOffset, end - dataOffset)
                dataOffset = end
            }
        }
    }

    fun formatSize(bytes: Long): String {
        if (bytes < 0) return "0 B"
        val units = arrayOf("B", "KiB", "MiB", "GiB", "TiB")
        var value = bytes.toDouble()
        var unit = 0
        while (value >= 1024.0 && unit + 1 < units.size) {
            value /= 1024.0
            unit++
        }
        return String.format(Locale.US, "%.1f %s", value, units[unit])
    }
}
