package com.ghostlock.app.data.ota

/**
 * Minimal payload.bin reader: parses install operations and handles decompression.
 * The manifest is scanned as raw protobuf with unknown fields skipped.
 */
object PayloadBinUtils {
    const val PAYLOAD_ENTRY = "payload.bin"
    const val HEADER_SIZE = 24 // magic (4), version (8), manifest size (8), signature size (4)

    const val OP_REPLACE = 0L
    const val OP_REPLACE_BZ = 1L
    const val OP_ZERO = 6L
    const val OP_DISCARD = 7L
    const val OP_REPLACE_XZ = 8L

    val PAYLOAD_MAGIC = byteArrayOf('C'.code.toByte(), 'r'.code.toByte(), 'A'.code.toByte(), 'U'.code.toByte())
    private const val MAX_MANIFEST_SIZE = 64L shl 20 // 64 MiB max manifest size

    data class PayloadHeader(val manifestSize: Long, val signatureSize: Long)

    /** Extent: (startBlock, numBlocks) */
    data class Extent(val startBlock: Long, val numBlocks: Long)

    /** One install operation in a partition. */
    data class PayloadOperation(
        val type: Long,
        val dataOffset: Long, // relative to the start of the payload's data blobs
        val dataLength: Long,
        val destExtents: List<Extent>,
    )

    fun parseHeader(header: ByteArray): PayloadHeader? {
        if (header.size < HEADER_SIZE) return null
        for (i in 0 until 4) {
            if (header[i] != PAYLOAD_MAGIC[i]) return null
        }
        val manifestSize = header.u64be(12)
        val signatureSize = header.u32be(20)
        if (manifestSize !in 1..MAX_MANIFEST_SIZE) return null
        return PayloadHeader(manifestSize, signatureSize)
    }

    /** Returns all partition names present in the manifest. */
    fun listPartitions(manifest: ByteArray): List<String> {
        val partitions = mutableListOf<String>()
        scanProto(manifest, 0, manifest.size) { field, wire, _, dataStart, dataEnd ->
            if (field == 13 && wire == 2) { // PartitionUpdate
                scanProto(manifest, dataStart, dataEnd) { pf, pw, _, pdStart, pdEnd ->
                    if (pf == 1 && pw == 2) { // partition_name
                        partitions.add(manifest.decodeToString(pdStart, pdEnd))
                        false
                    } else true
                }
            }
            true
        }
        return partitions
    }

    /** Returns the block size from the manifest, defaulting to 4096. */
    fun blockSize(manifest: ByteArray): Long {
        var blockSize = 4096L
        scanProto(manifest, 0, manifest.size) { field, wire, num, _, _ ->
            if (field == 3 && wire == 0) {
                blockSize = num
                false
            } else true
        }
        return if (blockSize > 0) blockSize else 4096L
    }

    /** The named partition's operations in image-write order; empty if absent. */
    fun partitionOperations(manifest: ByteArray, partition: String): List<PayloadOperation> {
        var operations = emptyList<PayloadOperation>()
        scanProto(manifest, 0, manifest.size) { field, wire, _, dataStart, dataEnd ->
            if (field != 13 || wire != 2) return@scanProto true // PartitionUpdate
            var name = ""
            val ops = ArrayList<PayloadOperation>()
            scanProto(manifest, dataStart, dataEnd) { pf, pw, _, pdStart, pdEnd ->
                when (pf) {
                    1 if pw == 2 -> name = manifest.decodeToString(pdStart, pdEnd)
                    8 if pw == 2 -> parseOperation(
                        manifest, pdStart, pdEnd
                    )?.let { ops.add(it) }
                }
                true
            }
            if (name == partition) {
                operations = ops
                false
            } else true
        }
        return operations
    }

    private fun parseOperation(buf: ByteArray, start: Int, end: Int): PayloadOperation? {
        var type = 0L
        var dataOffset = 0L
        var dataLength = 0L
        val extents = mutableListOf<Extent>()

        scanProto(buf, start, end) { field, wire, num, dStart, dEnd ->
            when (field) {
                1 if wire == 0 -> type = num
                2 if wire == 0 -> dataOffset = num
                3 if wire == 0 -> dataLength = num
                6 if wire == 2 -> { // dst_extents (repeated Extent)
                    var startBlock = 0L
                    var numBlocks = 0L
                    scanProto(buf, dStart, dEnd) { ef, ew, en, _, _ ->
                        when (ef) {
                            1 if ew == 0 -> startBlock = en
                            2 if ew == 0 -> numBlocks = en
                        }
                        true
                    }
                    if (numBlocks > 0) {
                        extents.add(Extent(startBlock, numBlocks))
                    }
                }
            }
            true
        }
        return if (extents.isNotEmpty()) PayloadOperation(
            type, dataOffset, dataLength, extents
        ) else null
    }

    /**
     * Walks protobuf wire format; returning false from [visit] stops the walk.
     */
    private fun scanProto(
        buf: ByteArray,
        start: Int,
        end: Int,
        visit: (field: Int, wire: Int, num: Long, dataStart: Int, dataEnd: Int) -> Boolean,
    ) {
        var pos = start
        while (pos < end) {
            val (tag, tagLen) = readVarint(buf, pos, end) ?: break
            pos += tagLen
            val wire = (tag and 7L).toInt()
            val field = (tag ushr 3).toInt()
            when (wire) {
                0 -> { // varint
                    val (num, numLen) = readVarint(buf, pos, end) ?: break
                    pos += numLen
                    if (!visit(field, wire, num, 0, 0)) return
                }

                1 -> { // 64-bit fixed
                    if (pos + 8 > end) break
                    val num = buf.u64le(pos)
                    pos += 8
                    if (!visit(field, wire, num, 0, 0)) return
                }

                2 -> { // length-delimited
                    val (len, lenBytes) = readVarint(buf, pos, end) ?: break
                    pos += lenBytes
                    if (len < 0 || pos + len > end) break
                    val dataStart = pos
                    val dataEnd = (pos + len).toInt()
                    pos = dataEnd
                    if (!visit(field, wire, len, dataStart, dataEnd)) return
                }

                5 -> { // 32-bit fixed
                    if (pos + 4 > end) break
                    val num = buf.u32le(pos)
                    pos += 4
                    if (!visit(field, wire, num, 0, 0)) return
                }

                else -> break // unsupported wire type, stop
            }
        }
    }

    private fun readVarint(buf: ByteArray, start: Int, end: Int): Pair<Long, Int>? {
        var value = 0L
        var shift = 0
        var pos = start
        while (pos < end && shift <= 63) {
            val b = buf[pos++].toLong()
            value = value or ((b and 0x7F) shl shift)
            if (b and 0x80 == 0L) return value to (pos - start)
            shift += 7
        }
        return null
    }

    private fun ByteArray.u32le(pos: Int): Long =
        (this[pos].toLong() and 0xFF) or ((this[pos + 1].toLong() and 0xFF) shl 8) or ((this[pos + 2].toLong() and 0xFF) shl 16) or ((this[pos + 3].toLong() and 0xFF) shl 24)

    private fun ByteArray.u64le(pos: Int): Long = u32le(pos) or (u32le(pos + 4) shl 32)

    private fun ByteArray.u32be(pos: Int): Long =
        (this[pos + 3].toLong() and 0xFF) or ((this[pos + 2].toLong() and 0xFF) shl 8) or ((this[pos + 1].toLong() and 0xFF) shl 16) or ((this[pos].toLong() and 0xFF) shl 24)

    fun ByteArray.u64be(pos: Int): Long = (u32be(pos) shl 32) or (u32be(pos + 4) and 0xFFFFFFFFL)
}
