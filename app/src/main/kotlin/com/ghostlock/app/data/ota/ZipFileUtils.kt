package com.ghostlock.app.data.ota

object ZipFileUtils {
    private const val CENSIG = 0x02014b50L         // "PK\001\002" - Central directory file header signature
    private const val LOCSIG = 0x04034b50L         // "PK\003\004" - Local file header signature
    private const val ENDSIG = 0x06054b50L         // "PK\005\006" - End of central directory record signature
    private const val ENDHDR = 22                  // Minimum size of end of central directory record
    private const val ZIP64_ENDSIG = 0x06064b50L   // "PK\006\006" - Zip64 end of central directory record signature
    private const val ZIP64_LOCSIG = 0x07064b50L   // "PK\006\007" - Zip64 end of central directory locator signature
    private const val ZIP64_LOCHDR = 20            // Size of Zip64 end of central directory locator
    private const val ZIP64_MAGICVAL = 0xFFFFFFFFL // Marker for Zip64 fields

    data class FileInfo(val offset: Long, val size: Long)

    fun locateCentralDirectory(bytes: ByteArray, fileLength: Long): FileInfo {
        val searchStartPos = bytes.size - ENDHDR
        var cenSize = -1L
        var cenOffset = -1L

        for (currentScanPos in searchStartPos downTo 0) {
            if ((bytes.getIntLe(currentScanPos).toLong() and 0xFFFFFFFFL) == ENDSIG) {
                val cenDirOffsetFieldPos = currentScanPos + 16
                val cenDirSizeFieldPos = currentScanPos + 12

                val offsetOfCentralDir = bytes.getIntLe(cenDirOffsetFieldPos).toLong() and 0xFFFFFFFFL
                val sizeOfCentralDir = bytes.getIntLe(cenDirSizeFieldPos).toLong() and 0xFFFFFFFFL

                if (offsetOfCentralDir == ZIP64_MAGICVAL || sizeOfCentralDir == ZIP64_MAGICVAL) {
                    val zip64LocatorPos = currentScanPos - ZIP64_LOCHDR
                    if (zip64LocatorPos >= 0 && (bytes.getIntLe(zip64LocatorPos).toLong() and 0xFFFFFFFFL) == ZIP64_LOCSIG) {
                        val zip64EocdRecordOffsetInFile = bytes.getLongLe(zip64LocatorPos + 8)
                        val zip64EocdRecordOffsetInBuffer = bytes.size - (fileLength - zip64EocdRecordOffsetInFile).toInt()
                        if (zip64EocdRecordOffsetInBuffer >= 0 && (zip64EocdRecordOffsetInBuffer + 56) <= bytes.size && (bytes.getIntLe(
                                zip64EocdRecordOffsetInBuffer
                            ).toLong() and 0xFFFFFFFFL) == ZIP64_ENDSIG
                        ) {
                            cenSize = bytes.getLongLe(zip64EocdRecordOffsetInBuffer + 40)
                            cenOffset = bytes.getLongLe(zip64EocdRecordOffsetInBuffer + 48)
                            break
                        }
                    }
                } else {
                    cenSize = sizeOfCentralDir
                    cenOffset = offsetOfCentralDir
                    break
                }
            }
        }
        return FileInfo(cenOffset, cenSize)
    }

    data class CdEntry(
        val fileName: String,
        val localHeaderOffset: Long,
        val compressedSize: Long,
        val uncompressedSize: Long,
        val method: Int,
    )

    fun locateEntries(bytes: ByteArray, fileNames: Set<String>): Map<String, CdEntry> {
        val results = HashMap<String, CdEntry>(fileNames.size)
        var pos = 0
        while (pos + 46 <= bytes.size) {
            if ((bytes.getIntLe(pos).toLong() and 0xFFFFFFFFL) != CENSIG) break

            val method = bytes.getShortLe(pos + 10).toInt() and 0xFFFF
            var compressedSize = bytes.getIntLe(pos + 20).toLong() and 0xFFFFFFFFL
            var uncompressedSize = bytes.getIntLe(pos + 24).toLong() and 0xFFFFFFFFL
            val fileNameLength = bytes.getShortLe(pos + 28).toInt() and 0xFFFF
            val extraFieldLength = bytes.getShortLe(pos + 30).toInt() and 0xFFFF
            val fileCommentLength = bytes.getShortLe(pos + 32).toInt() and 0xFFFF
            var localHeaderOffset = bytes.getIntLe(pos + 42).toLong() and 0xFFFFFFFFL

            val fileNameStartPos = pos + 46
            if (fileNameStartPos + fileNameLength > bytes.size) break

            val currentFileName = bytes.decodeToString(fileNameStartPos, fileNameStartPos + fileNameLength)
            if (currentFileName in fileNames) {
                val extraStart = fileNameStartPos + fileNameLength
                val extraEnd = minOf(extraStart + extraFieldLength, bytes.size)
                if (uncompressedSize == ZIP64_MAGICVAL || compressedSize == ZIP64_MAGICVAL || localHeaderOffset == ZIP64_MAGICVAL) {
                    var extraPos = extraStart
                    while (extraPos + 4 <= extraEnd) {
                        val id = bytes.getShortLe(extraPos).toInt() and 0xFFFF
                        val size = bytes.getShortLe(extraPos + 2).toInt() and 0xFFFF
                        val dataStart = extraPos + 4
                        if (dataStart + size > extraEnd) break
                        if (id == 0x0001) {
                            var fieldPos = dataStart
                            if (uncompressedSize == ZIP64_MAGICVAL && fieldPos + 8 <= dataStart + size) {
                                uncompressedSize = bytes.getLongLe(fieldPos); fieldPos += 8
                            }
                            if (compressedSize == ZIP64_MAGICVAL && fieldPos + 8 <= dataStart + size) {
                                compressedSize = bytes.getLongLe(fieldPos); fieldPos += 8
                            }
                            if (localHeaderOffset == ZIP64_MAGICVAL && fieldPos + 8 <= dataStart + size) {
                                localHeaderOffset = bytes.getLongLe(fieldPos)
                            }
                            break
                        }
                        extraPos = dataStart + size
                    }
                }
                results[currentFileName] = CdEntry(
                    fileName = currentFileName,
                    localHeaderOffset = localHeaderOffset,
                    compressedSize = compressedSize,
                    uncompressedSize = uncompressedSize,
                    method = method,
                )
                if (results.size == fileNames.size) break
            }
            pos = fileNameStartPos + fileNameLength + extraFieldLength + fileCommentLength
        }
        return results
    }

    fun locateLocalFileOffset(bytes: ByteArray): Long {
        if (bytes.size >= 30 && (bytes.getIntLe(0).toLong() and 0xFFFFFFFFL) == LOCSIG) {
            val fileNameLength = bytes.getShortLe(26).toInt() and 0xFFFF
            val extraFieldLength = bytes.getShortLe(28).toInt() and 0xFFFF
            return (30L + fileNameLength + extraFieldLength)
        }
        return -1L
    }

    private fun ByteArray.getIntLe(pos: Int): Int {
        return (this[pos].toInt() and 0xFF) or ((this[pos + 1].toInt() and 0xFF) shl 8) or ((this[pos + 2].toInt() and 0xFF) shl 16) or ((this[pos + 3].toInt() and 0xFF) shl 24)
    }

    private fun ByteArray.getShortLe(pos: Int): Short {
        return ((this[pos].toInt() and 0xFF) or ((this[pos + 1].toInt() and 0xFF) shl 8)).toShort()
    }

    private fun ByteArray.getLongLe(pos: Int): Long {
        return (getIntLe(pos).toLong() and ZIP64_MAGICVAL) or (getIntLe(pos + 4).toLong() shl 32)
    }
}
