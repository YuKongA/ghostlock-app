package com.ghostlock.plugin.core

import kotlinx.serialization.KSerializer
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

object PluginMetadataCodec {
    private const val CONTENT_DOMAIN = "GhostLockPluginContent/1"

    private val json = Json {
        ignoreUnknownKeys = false
        isLenient = false
        allowSpecialFloatingPointValues = false
        explicitNulls = true
        encodeDefaults = true
        prettyPrint = false
    }

    fun decodeManifest(source: String): PluginPackageManifest =
        decodeStrict(PluginPackageManifest.serializer(), source).normalized()

    fun encodeManifest(manifest: PluginPackageManifest): String =
        json.encodeToString(manifest.normalized())

    fun pluginContentSha256(manifest: PluginPackageManifest): PluginContentSha256 {
        val normalizedBytes = encodeManifest(manifest).encodeToByteArray()
        val domainBytes = CONTENT_DOMAIN.encodeToByteArray()
        val input = ByteArray(domainBytes.size + 1 + normalizedBytes.size)
        domainBytes.copyInto(input)
        input[domainBytes.size] = 0
        normalizedBytes.copyInto(input, domainBytes.size + 1)
        return PluginContentSha256(Sha256.digest(input).toLowerHex())
    }

    fun decodeBuiltinIndex(source: String): BuiltinPluginIndex =
        decodeStrict(BuiltinPluginIndex.serializer(), source).normalized()

    fun encodeBuiltinIndex(index: BuiltinPluginIndex): String =
        json.encodeToString(index.normalized())

    fun decodeCatalogSnapshot(source: String): PluginCatalogSnapshot =
        decodeStrict(PluginCatalogSnapshot.serializer(), source).normalized()

    fun encodeCatalogSnapshot(snapshot: PluginCatalogSnapshot): String =
        json.encodeToString(snapshot.normalized())

    private fun <T> decodeStrict(serializer: KSerializer<T>, source: String): T {
        DuplicateMemberScanner(source).scan()
        return try {
            json.decodeFromString(serializer, source)
        } catch (error: IllegalArgumentException) {
            throw error
        } catch (error: Exception) {
            throw IllegalArgumentException("invalid plugin metadata JSON", error)
        }
    }
}

private class DuplicateMemberScanner(private val source: String) {
    private var index = 0

    fun scan() {
        skipWhitespace()
        parseValue()
        skipWhitespace()
        require(index == source.length) { "trailing JSON content at offset $index" }
    }

    private fun parseValue() {
        require(index < source.length) { "missing JSON value" }
        when (source[index]) {
            '{' -> parseObject()
            '[' -> parseArray()
            '"' -> parseString()
            't' -> consumeLiteral("true")
            'f' -> consumeLiteral("false")
            'n' -> consumeLiteral("null")
            '-', in '0'..'9' -> parseNumber()
            else -> throw IllegalArgumentException("invalid JSON value at offset $index")
        }
    }

    private fun parseObject() {
        index++
        skipWhitespace()
        if (consumeIf('}')) return
        val names = mutableSetOf<String>()
        while (true) {
            require(index < source.length && source[index] == '"') {
                "object key must be a string at offset $index"
            }
            val name = parseString()
            require(names.add(name)) { "duplicate JSON member: $name" }
            skipWhitespace()
            require(consumeIf(':')) { "missing ':' after object key at offset $index" }
            skipWhitespace()
            parseValue()
            skipWhitespace()
            if (consumeIf('}')) return
            require(consumeIf(',')) { "missing ',' in object at offset $index" }
            skipWhitespace()
        }
    }

    private fun parseArray() {
        index++
        skipWhitespace()
        if (consumeIf(']')) return
        while (true) {
            parseValue()
            skipWhitespace()
            if (consumeIf(']')) return
            require(consumeIf(',')) { "missing ',' in array at offset $index" }
            skipWhitespace()
        }
    }

    private fun parseString(): String {
        require(consumeIf('"')) { "missing string quote at offset $index" }
        val decoded = StringBuilder()
        while (index < source.length) {
            val character = source[index++]
            when {
                character == '"' -> return decoded.toString()
                character == '\\' -> decoded.append(parseEscape())
                character.code < 0x20 -> throw IllegalArgumentException(
                    "control character in JSON string at offset ${index - 1}",
                )
                else -> decoded.append(character)
            }
        }
        throw IllegalArgumentException("unterminated JSON string")
    }

    private fun parseEscape(): Char {
        require(index < source.length) { "unterminated JSON escape" }
        return when (val escaped = source[index++]) {
            '"', '\\', '/' -> escaped
            'b' -> '\b'
            'f' -> '\u000c'
            'n' -> '\n'
            'r' -> '\r'
            't' -> '\t'
            'u' -> parseUnicodeEscape()
            else -> throw IllegalArgumentException("invalid JSON escape: \\$escaped")
        }
    }

    private fun parseUnicodeEscape(): Char {
        require(index + 4 <= source.length) { "short JSON unicode escape" }
        var value = 0
        repeat(4) {
            val digit = source[index++].digitToIntOrNull(16)
                ?: throw IllegalArgumentException("invalid JSON unicode escape")
            value = (value shl 4) or digit
        }
        return value.toChar()
    }

    private fun parseNumber() {
        consumeIf('-')
        require(index < source.length) { "incomplete JSON number" }
        if (source[index] == '0') {
            index++
            require(index == source.length || source[index] !in '0'..'9') {
                "leading zero in JSON number"
            }
        } else {
            require(source[index] in '1'..'9') { "invalid JSON number at offset $index" }
            while (index < source.length && source[index] in '0'..'9') index++
        }
        if (consumeIf('.')) {
            require(index < source.length && source[index] in '0'..'9') {
                "missing JSON fraction"
            }
            while (index < source.length && source[index] in '0'..'9') index++
        }
        if (index < source.length && (source[index] == 'e' || source[index] == 'E')) {
            index++
            if (index < source.length && (source[index] == '+' || source[index] == '-')) index++
            require(index < source.length && source[index] in '0'..'9') {
                "missing JSON exponent"
            }
            while (index < source.length && source[index] in '0'..'9') index++
        }
    }

    private fun consumeLiteral(literal: String) {
        require(source.regionMatches(index, literal, 0, literal.length)) {
            "invalid JSON literal at offset $index"
        }
        index += literal.length
    }

    private fun skipWhitespace() {
        while (index < source.length && source[index] in JSON_WHITESPACE) index++
    }

    private fun consumeIf(expected: Char): Boolean {
        if (index >= source.length || source[index] != expected) return false
        index++
        return true
    }

    private companion object {
        val JSON_WHITESPACE = setOf(' ', '\t', '\n', '\r')
    }
}

private object Sha256 {
    private val roundConstants = intArrayOf(
        0x428a2f98, 0x71374491, 0xb5c0fbcf.toInt(), 0xe9b5dba5.toInt(),
        0x3956c25b, 0x59f111f1, 0x923f82a4.toInt(), 0xab1c5ed5.toInt(),
        0xd807aa98.toInt(), 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe.toInt(), 0x9bdc06a7.toInt(), 0xc19bf174.toInt(),
        0xe49b69c1.toInt(), 0xefbe4786.toInt(), 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152.toInt(), 0xa831c66d.toInt(), 0xb00327c8.toInt(), 0xbf597fc7.toInt(),
        0xc6e00bf3.toInt(), 0xd5a79147.toInt(), 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e.toInt(), 0x92722c85.toInt(),
        0xa2bfe8a1.toInt(), 0xa81a664b.toInt(), 0xc24b8b70.toInt(), 0xc76c51a3.toInt(),
        0xd192e819.toInt(), 0xd6990624.toInt(), 0xf40e3585.toInt(), 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814.toInt(), 0x8cc70208.toInt(),
        0x90befffa.toInt(), 0xa4506ceb.toInt(), 0xbef9a3f7.toInt(), 0xc67178f2.toInt(),
    )

    fun digest(input: ByteArray): ByteArray {
        val paddedLength = ((input.size.toLong() + 9L + 63L) / 64L) * 64L
        require(paddedLength <= Int.MAX_VALUE) { "SHA-256 input is too large" }
        val padded = ByteArray(paddedLength.toInt())
        input.copyInto(padded)
        padded[input.size] = 0x80.toByte()
        val bitLength = input.size.toLong() * 8L
        repeat(8) { byteIndex ->
            padded[padded.lastIndex - byteIndex] = (bitLength ushr (byteIndex * 8)).toByte()
        }

        var h0 = 0x6a09e667
        var h1 = 0xbb67ae85.toInt()
        var h2 = 0x3c6ef372
        var h3 = 0xa54ff53a.toInt()
        var h4 = 0x510e527f
        var h5 = 0x9b05688c.toInt()
        var h6 = 0x1f83d9ab
        var h7 = 0x5be0cd19
        val words = IntArray(64)

        for (blockOffset in padded.indices step 64) {
            for (word in 0 until 16) {
                val offset = blockOffset + word * 4
                words[word] = ((padded[offset].toInt() and 0xff) shl 24) or
                    ((padded[offset + 1].toInt() and 0xff) shl 16) or
                    ((padded[offset + 2].toInt() and 0xff) shl 8) or
                    (padded[offset + 3].toInt() and 0xff)
            }
            for (word in 16 until 64) {
                val s0 = words[word - 15].rotateRight(7) xor
                    words[word - 15].rotateRight(18) xor (words[word - 15] ushr 3)
                val s1 = words[word - 2].rotateRight(17) xor
                    words[word - 2].rotateRight(19) xor (words[word - 2] ushr 10)
                words[word] = words[word - 16] + s0 + words[word - 7] + s1
            }

            var a = h0
            var b = h1
            var c = h2
            var d = h3
            var e = h4
            var f = h5
            var g = h6
            var h = h7
            for (round in 0 until 64) {
                val sum1 = e.rotateRight(6) xor e.rotateRight(11) xor e.rotateRight(25)
                val choice = (e and f) xor (e.inv() and g)
                val temporary1 = h + sum1 + choice + roundConstants[round] + words[round]
                val sum0 = a.rotateRight(2) xor a.rotateRight(13) xor a.rotateRight(22)
                val majority = (a and b) xor (a and c) xor (b and c)
                val temporary2 = sum0 + majority
                h = g
                g = f
                f = e
                e = d + temporary1
                d = c
                c = b
                b = a
                a = temporary1 + temporary2
            }
            h0 += a
            h1 += b
            h2 += c
            h3 += d
            h4 += e
            h5 += f
            h6 += g
            h7 += h
        }

        return intArrayOf(h0, h1, h2, h3, h4, h5, h6, h7).toBigEndianBytes()
    }
}

private fun Int.rotateRight(bits: Int): Int = (this ushr bits) or (this shl (32 - bits))

private fun IntArray.toBigEndianBytes(): ByteArray = ByteArray(size * 4).also { output ->
    forEachIndexed { index, value ->
        val offset = index * 4
        output[offset] = (value ushr 24).toByte()
        output[offset + 1] = (value ushr 16).toByte()
        output[offset + 2] = (value ushr 8).toByte()
        output[offset + 3] = value.toByte()
    }
}

private fun ByteArray.toLowerHex(): String = buildString(size * 2) {
    this@toLowerHex.forEach { byte ->
        val value = byte.toInt() and 0xff
        append(HEX[value ushr 4])
        append(HEX[value and 0x0f])
    }
}

private const val HEX = "0123456789abcdef"
