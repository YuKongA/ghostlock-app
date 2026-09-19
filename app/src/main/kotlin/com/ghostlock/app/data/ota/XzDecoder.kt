package com.ghostlock.app.data.ota

/**
 * Pure-Kotlin XZ + LZMA2 decoder, the only combination update_engine payloads
 * use. Integrity checks are parsed past but not verified.
 */
object XzDecoder {

    /**
     * Decodes at most [maxOutput] bytes; with [allowTruncated] the input may
     * be a stream prefix and whatever decoded whole is returned.
     */
    fun decode(
        input: ByteArray, maxOutput: Int = Int.MAX_VALUE, allowTruncated: Boolean = false
    ): ByteArray {
        val reader = ByteReader(input)
        val output = OutputBuffer(maxOutput)
        try {
            decodeStream(reader, output)
        } catch (e: XzException) {
            if (!(allowTruncated && e is TruncatedXzException)) throw e
        }
        return output.toByteArray()
    }

    private fun decodeStream(reader: ByteReader, output: OutputBuffer) {
        val magic = byteArrayOf(
            0xFD.toByte(), '7'.code.toByte(), 'z'.code.toByte(), 'X'.code.toByte(), 'Z'.code.toByte(), 0
        )
        for (b in magic) {
            if (reader.u8() != (b.toInt() and 0xFF)) throw XzException("not an XZ stream")
        }
        if (reader.u8() != 0) throw XzException("unsupported stream flags")
        val checkType = reader.u8()
        if (checkType > 15) throw XzException("invalid check type")
        reader.skip(4) // CRC32 of the stream flags

        val checkSize = if (checkType == 0) 0 else 4 shl ((checkType - 1) / 3)

        while (!output.isFull) {
            val headerSizeByte = reader.u8()
            if (headerSizeByte == 0) return // index indicator: no more blocks
            decodeBlock(reader, output, headerSizeByte)
            if (output.isFull) return
            // block padding to 4-byte alignment, then the check
            while (reader.position % 4 != 0L) {
                if (reader.u8() != 0) throw XzException("nonzero block padding")
            }
            reader.skip(checkSize)
        }
    }

    private fun decodeBlock(reader: ByteReader, output: OutputBuffer, headerSizeByte: Int) {
        val headerSize = (headerSizeByte + 1) * 4
        val header = reader.bytes(headerSize - 1)

        val flags = header[0].toInt() and 0xFF
        val filterCount = (flags and 0x03) + 1
        if (flags and 0x3C != 0) throw XzException("reserved block flags set")

        var pos = 1
        fun varint(): Long {
            var value = 0L
            var shift = 0
            while (true) {
                if (pos >= header.size) throw XzException("truncated block header")
                val b = header[pos++].toInt() and 0xFF
                value = value or ((b and 0x7F).toLong() shl shift)
                if (b and 0x80 == 0) return value
                shift += 7
                if (shift > 62) throw XzException("varint overflow")
            }
        }

        if (flags and 0x40 != 0) varint() // compressed size, unused
        if (flags and 0x80 != 0) varint() // uncompressed size, unused

        var isLzma2 = false
        repeat(filterCount) {
            val filterId = varint()
            val propsSize = varint().toInt()
            if (propsSize < 0 || pos + propsSize > header.size) throw XzException("truncated filter properties")
            pos += propsSize
            if (filterId == 0x21L) isLzma2 = true else throw XzException("unsupported filter 0x${filterId.toString(16)}")
        }
        if (!isLzma2) throw XzException("no LZMA2 filter present")

        Lzma2Decoder(reader, output).run()
    }
}

internal open class XzException(message: String) : Exception(message)
internal class TruncatedXzException : XzException("truncated XZ input")

internal class ByteReader(private val buf: ByteArray) {
    var position = 0L
        private set
    private var pos = 0

    fun u8(): Int {
        if (pos >= buf.size) throw TruncatedXzException()
        position++
        return buf[pos++].toInt() and 0xFF
    }

    fun bytes(count: Int): ByteArray {
        if (count < 0 || pos + count > buf.size) throw TruncatedXzException()
        val out = buf.copyOfRange(pos, pos + count)
        pos += count
        position += count
        return out
    }

    fun skip(count: Int) {
        if (pos + count > buf.size) throw TruncatedXzException()
        pos += count
        position += count
    }
}

/** Decoded output; doubles as the LZ dictionary that matches copy from. */
internal class OutputBuffer(private val limit: Int) {
    private var buf = ByteArray(minOf(limit.toLong(), 64L * 1024).toInt())
    var size = 0
        private set

    val isFull: Boolean get() = size >= limit

    private fun ensure(extra: Int) {
        val needed = size + extra
        if (needed <= buf.size) return
        var newSize = maxOf(buf.size * 2, needed)
        if (limit < Int.MAX_VALUE) newSize = minOf(newSize.toLong(), limit.toLong() + 273).toInt()
        buf = buf.copyOf(maxOf(newSize, needed))
    }

    fun put(b: Int) {
        ensure(1)
        buf[size++] = b.toByte()
    }

    fun append(bytes: ByteArray) {
        ensure(bytes.size)
        bytes.copyInto(buf, size)
        size += bytes.size
    }

    /** The byte [dist] positions back from the write head; dist >= 1. */
    fun byteBack(dist: Int): Int {
        if (dist > size) throw XzException("match distance beyond decoded data")
        return buf[size - dist].toInt() and 0xFF
    }

    fun copyMatch(dist: Int, len: Int) {
        if (dist > size) throw XzException("match distance beyond decoded data")
        ensure(len)
        var from = size - dist
        repeat(len) {
            buf[size++] = buf[from++]
        }
    }

    fun toByteArray(): ByteArray = buf.copyOf(minOf(size, limit))
}

/** LZMA2 chunk layer: control bytes, per-chunk sizes, state/props resets. */
internal class Lzma2Decoder(private val reader: ByteReader, private val output: OutputBuffer) {
    private var lzma: LzmaDecoder? = null
    private var props = -1

    fun run() {
        while (!output.isFull) {
            when (val control = reader.u8()) {
                0x00 -> return // end of LZMA2 data
                0x01, 0x02 -> {
                    val size = (reader.u8() shl 8 or reader.u8()) + 1
                    output.append(reader.bytes(size))
                    lzma = null // an uncompressed chunk invalidates LZMA state
                }

                in 0x03..0x7F -> throw XzException("invalid LZMA2 control byte $control")
                else -> {
                    val unpackSize = ((control and 0x1F) shl 16) + (reader.u8() shl 8) + reader.u8() + 1
                    val packSize = (reader.u8() shl 8) + reader.u8() + 1
                    val resetMode = (control ushr 5) and 3
                    if (resetMode >= 2) props = reader.u8()
                    if (resetMode >= 1 || lzma == null) {
                        if (props < 0) throw XzException("LZMA chunk before properties were set")
                        lzma = LzmaDecoder(props)
                    }
                    val rc = RangeDecoder(reader.bytes(packSize))
                    lzma!!.decodeChunk(rc, output, unpackSize)
                }
            }
        }
    }
}

/** Binary range decoder, self-contained per LZMA2 chunk. */
internal class RangeDecoder(private val buf: ByteArray) {
    private var pos = 0
    private var range = -1 // 0xFFFFFFFF
    private var code = 0

    init {
        if (buf.size < 5 || buf[0].toInt() != 0) throw XzException("malformed range coder init")
        pos = 1
        repeat(4) { code = (code shl 8) or nextByte() }
    }

    private fun nextByte(): Int {
        // Normalization may read past the chunk payload; zeros there match the reference decoders.
        return if (pos < buf.size) buf[pos++].toInt() and 0xFF else 0
    }

    fun decodeBit(probs: ShortArray, index: Int): Int {
        val prob = probs[index].toInt()
        val bound = (range ushr 11) * prob
        val bit: Int
        if (code.toUInt() < bound.toUInt()) {
            probs[index] = (prob + ((2048 - prob) ushr 5)).toShort()
            range = bound
            bit = 0
        } else {
            probs[index] = (prob - (prob ushr 5)).toShort()
            code -= bound
            range -= bound
            bit = 1
        }
        if (range.toUInt() < TOP) {
            range = range shl 8
            code = (code shl 8) or nextByte()
        }
        return bit
    }

    fun decodeDirectBits(count: Int): Int {
        var result = 0
        repeat(count) {
            range = range ushr 1
            code -= range
            val t = 0 - (code ushr 31)
            code += range and t
            if (range.toUInt() < TOP) {
                range = range shl 8
                code = (code shl 8) or nextByte()
            }
            result = (result shl 1) + t + 1
        }
        return result
    }

    fun decodeBitTree(probs: ShortArray, numBits: Int): Int {
        var m = 1
        repeat(numBits) { m = (m shl 1) + decodeBit(probs, m) }
        return m - (1 shl numBits)
    }

    fun decodeBitTreeReverse(probs: ShortArray, offset: Int, numBits: Int): Int {
        var m = 1
        var symbol = 0
        for (i in 0 until numBits) {
            val bit = decodeBit(probs, offset + m)
            m = (m shl 1) + bit
            symbol = symbol or (bit shl i)
        }
        return symbol
    }

    private companion object {
        const val TOP = 0x0100_0000u
    }
}

/** Port of 7-Zip's LzmaSpec.cpp; state persists across chunks until LZMA2 resets it. */
internal class LzmaDecoder(props: Int) {
    private val lc: Int
    private val lp: Int
    private val lpMask: Int
    private val pbMask: Int

    init {
        if (props >= 9 * 5 * 5) throw XzException("invalid LZMA properties byte $props")
        lc = props % 9
        val rest = props / 9
        lp = rest % 5
        lpMask = (1 shl lp) - 1
        pbMask = (1 shl (rest / 5)) - 1
    }

    private var state = 0
    private var rep0 = 0
    private var rep1 = 0
    private var rep2 = 0
    private var rep3 = 0

    private val isMatch = newProbs(12 shl 4)
    private val isRep = newProbs(12)
    private val isRepG0 = newProbs(12)
    private val isRepG1 = newProbs(12)
    private val isRepG2 = newProbs(12)
    private val isRep0Long = newProbs(12 shl 4)
    private val posSlot = Array(4) { newProbs(64) }
    private val specPos = newProbs(115)
    private val align = newProbs(16)
    private val literal = newProbs(0x300 shl (lc + lp))
    private val lenChoice = newProbs(2)
    private val lenLow = Array(16) { newProbs(8) }
    private val lenMid = Array(16) { newProbs(8) }
    private val lenHigh = newProbs(256)
    private val repLenChoice = newProbs(2)
    private val repLenLow = Array(16) { newProbs(8) }
    private val repLenMid = Array(16) { newProbs(8) }
    private val repLenHigh = newProbs(256)

    private fun newProbs(size: Int) = ShortArray(size) { 1024 }

    fun decodeChunk(rc: RangeDecoder, out: OutputBuffer, unpackSize: Int) {
        val chunkEnd = out.size + unpackSize
        while (out.size < chunkEnd && !out.isFull) {
            val posState = out.size and pbMask
            if (rc.decodeBit(isMatch, (state shl 4) + posState) == 0) {
                decodeLiteral(rc, out)
                continue
            }

            val len: Int
            if (rc.decodeBit(isRep, state) == 0) {
                rep3 = rep2; rep2 = rep1; rep1 = rep0
                len = decodeLen(rc, lenChoice, lenLow, lenMid, lenHigh, posState)
                rep0 = decodeDistance(rc, len)
                if (rep0 == -1) return // end-of-stream marker
                state = if (state < 7) 7 else 10
            } else {
                if (rc.decodeBit(isRepG0, state) == 0) {
                    if (rc.decodeBit(isRep0Long, (state shl 4) + posState) == 0) {
                        state = if (state < 7) 9 else 11
                        out.put(out.byteBack(rep0 + 1))
                        continue
                    }
                } else {
                    val dist: Int
                    if (rc.decodeBit(isRepG1, state) == 0) {
                        dist = rep1
                    } else {
                        if (rc.decodeBit(isRepG2, state) == 0) {
                            dist = rep2
                        } else {
                            dist = rep3
                            rep3 = rep2
                        }
                        rep2 = rep1
                    }
                    rep1 = rep0
                    rep0 = dist
                }
                len = decodeLen(rc, repLenChoice, repLenLow, repLenMid, repLenHigh, posState)
                state = if (state < 7) 8 else 11
            }
            out.copyMatch(rep0 + 1, len)
        }
    }

    private fun decodeLiteral(rc: RangeDecoder, out: OutputBuffer) {
        val prevByte = if (out.size == 0) 0 else out.byteBack(1)
        val litState = ((out.size and lpMask) shl lc) + (prevByte ushr (8 - lc))
        val offset = 0x300 * litState

        var symbol = 1
        if (state >= 7) {
            var matchByte = out.byteBack(rep0 + 1)
            while (symbol < 0x100) {
                val matchBit = (matchByte ushr 7) and 1
                matchByte = matchByte shl 1
                val bit = rc.decodeBit(literal, offset + ((1 + matchBit) shl 8) + symbol)
                symbol = (symbol shl 1) or bit
                if (matchBit != bit) break
            }
        }
        while (symbol < 0x100) {
            symbol = (symbol shl 1) or rc.decodeBit(literal, offset + symbol)
        }
        out.put(symbol and 0xFF)

        state = when {
            state < 4 -> 0
            state < 10 -> state - 3
            else -> state - 6
        }
    }

    private fun decodeLen(
        rc: RangeDecoder,
        choice: ShortArray,
        low: Array<ShortArray>,
        mid: Array<ShortArray>,
        high: ShortArray,
        posState: Int,
    ): Int {
        return 2 + when {
            rc.decodeBit(choice, 0) == 0 -> rc.decodeBitTree(low[posState], 3)
            rc.decodeBit(choice, 1) == 0 -> 8 + rc.decodeBitTree(mid[posState], 3)
            else -> 16 + rc.decodeBitTree(high, 8)
        }
    }

    private fun decodeDistance(rc: RangeDecoder, len: Int): Int {
        val lenToPosState = minOf(len - 2, 3)
        val slot = rc.decodeBitTree(posSlot[lenToPosState], 6)
        if (slot < 4) return slot

        val numDirectBits = (slot ushr 1) - 1
        var dist = (2 or (slot and 1)) shl numDirectBits
        if (slot < 14) {
            dist += rc.decodeBitTreeReverse(specPos, dist - slot, numDirectBits)
        } else {
            dist += rc.decodeDirectBits(numDirectBits - 4) shl 4
            dist += rc.decodeBitTreeReverse(align, 0, 4)
        }
        return dist
    }
}
