package com.ghostlock.app.data.profile

import org.msgpack.core.MessagePack
import org.msgpack.core.MessagePacker
import java.math.BigInteger

/**
 * GLKv3 logical value (native `profile::glkv3::Value`): exactly the MessagePack
 * value types the wire model allows. There is no float/map/nil/ext variant;
 * a section is a map of string keys to these values, never a value itself.
 *
 * [UInt] and [Int] are distinct wire types even though a non-negative [Int]
 * shares the shortest positive encoding with [UInt] (native
 * `mpack_write_i64` delegates to the unsigned writer for values >= 0).
 */
sealed interface Glkv3Value {
    /** MessagePack uint: 0..2^64-1. */
    data class UInt(val value: ULong) : Glkv3Value

    /** MessagePack signed int. */
    data class Int(val value: Long) : Glkv3Value

    data class Bool(val value: Boolean) : Glkv3Value

    /** MessagePack str; [value] is encoded as UTF-8. */
    data class Str(val value: String) : Glkv3Value

    /** MessagePack bin. */
    class Bin(val value: ByteArray) : Glkv3Value {
        override fun equals(other: Any?): Boolean = other is Bin && value.contentEquals(other.value)
        override fun hashCode(): kotlin.Int = value.contentHashCode()
        override fun toString(): String = "Bin(${value.size} bytes)"
    }

    data class Array(val elements: List<Glkv3Value>) : Glkv3Value
}

/** One section entry; canonical order is determined by the encoder, not this list. */
data class Glkv3Entry(val key: String, val value: Glkv3Value)

/** A named section; canonical order is determined by the encoder, not this list. */
data class Glkv3Section(val name: String, val entries: List<Glkv3Entry>)

/**
 * Logical GLKv3 document. The root map always carries `schema` and `sections`;
 * `release`/`terminal`/`backend`/`route` are present only when non-null,
 * because presence is expressed by key occurrence (an omitted key is not
 * 0/false/empty). See `docs/analysis/wire-transport-model.md`.
 */
data class Glkv3Document(
    val schema: ULong = Glkv3Encoder.SCHEMA_VERSION,
    val release: String? = null,
    val terminal: String? = null,
    val backend: String? = null,
    val route: String? = null,
    val sections: List<Glkv3Section> = emptyList(),
)

/**
 * Canonical GLKv3 MessagePack encoder, byte-compatible with the native
 * `glkv3::encode` (MPack) writer:
 *
 * - root map keys and section/entry keys are sorted by **UTF-8 byte order**
 *   (native compares `std::string_view` bytes; Kotlin strings are UTF-16, so
 *   [compareUtf8Bytes] is used instead of [String.compareTo]);
 * - integers use the shortest form (`org.msgpack:msgpack-core` `packLong` /
 *   `packBigInteger`, matching `mpack_write_u64`/`mpack_write_i64`);
 * - no floats, no extension types, no map values.
 *
 * The encoder is pure and deterministic: the same logical [Glkv3Document]
 * yields the same bytes regardless of list insertion order. It is the
 * production writer for `exportKernelProfiles` and the app native-document
 * path (GLKv3-4); v2 is read-only.
 */
object Glkv3Encoder {
    /** GLKv3 document schema, mirroring native `glkv3::kSchemaVersion`. */
    const val SCHEMA_VERSION: ULong = 3uL

    /** Canonical UTF-8 byte order, used for every map key. */
    private val UTF8_ORDER: Comparator<String> =
        Comparator { left, right -> compareUtf8Bytes(left, right) }

    /** Encodes [document] as a canonical GLKv3 MessagePack document. */
    fun encode(document: Glkv3Document): ByteArray {
        val packer = MessagePack.newDefaultBufferPacker()
        return try {
            writeRoot(packer, document)
            packer.toByteArray()
        } finally {
            packer.close()
        }
    }

    /**
     * Unsigned (and therefore UTF-8) byte order comparison, matching native
     * `std::char_traits<char>::compare`. Kotlin's [String.compareTo] compares
     * UTF-16 code units, which orders astral/supplementary characters
     * differently.
     */
    fun compareUtf8Bytes(left: String, right: String): Int {
        val leftBytes = left.toByteArray(Charsets.UTF_8)
        val rightBytes = right.toByteArray(Charsets.UTF_8)
        val shared = minOf(leftBytes.size, rightBytes.size)
        for (index in 0 until shared) {
            val a = leftBytes[index].toInt() and 0xFF
            val b = rightBytes[index].toInt() and 0xFF
            if (a != b) return a - b
        }
        return leftBytes.size - rightBytes.size
    }

    private fun writeRoot(packer: MessagePacker, document: Glkv3Document) {
        val keys = ArrayList<String>(6)
        document.backend?.let { keys += "backend" }
        document.release?.let { keys += "release" }
        document.route?.let { keys += "route" }
        keys += "schema"
        keys += "sections"
        document.terminal?.let { keys += "terminal" }
        keys.sortWith(UTF8_ORDER)

        packer.packMapHeader(keys.size)
        for (key in keys) {
            packer.packString(key)
            when (key) {
                "backend" -> packer.packString(document.backend!!)
                "release" -> packer.packString(document.release!!)
                "route" -> packer.packString(document.route!!)
                "schema" -> writeUnsigned(packer, document.schema)
                "sections" -> writeSections(packer, document.sections)
                "terminal" -> packer.packString(document.terminal!!)
                else -> error("unreachable GLKv3 root key: $key")
            }
        }
    }

    private fun writeSections(packer: MessagePacker, sections: List<Glkv3Section>) {
        val sorted = sections.sortedWith(compareBy(UTF8_ORDER) { it.name })
        packer.packMapHeader(sorted.size)
        for (section in sorted) {
            packer.packString(section.name)
            writeEntries(packer, section.entries)
        }
    }

    private fun writeEntries(packer: MessagePacker, entries: List<Glkv3Entry>) {
        val sorted = entries.sortedWith(compareBy(UTF8_ORDER) { it.key })
        packer.packMapHeader(sorted.size)
        for (entry in sorted) {
            packer.packString(entry.key)
            writeValue(packer, entry.value)
        }
    }

    private fun writeValue(packer: MessagePacker, value: Glkv3Value) {
        when (value) {
            is Glkv3Value.UInt -> writeUnsigned(packer, value.value)
            is Glkv3Value.Int -> packer.packLong(value.value)
            is Glkv3Value.Bool -> packer.packBoolean(value.value)
            is Glkv3Value.Str -> packer.packString(value.value)
            is Glkv3Value.Bin -> {
                packer.packBinaryHeader(value.value.size)
                packer.writePayload(value.value)
            }

            is Glkv3Value.Array -> {
                packer.packArrayHeader(value.elements.size)
                for (element in value.elements) writeValue(packer, element)
            }
        }
    }

    /**
     * Shortest unsigned form. Values above [Long.MAX_VALUE] need
     * [MessagePacker.packBigInteger] because [MessagePacker.packLong] cannot
     * represent them; the 64-bit BigInteger path emits uint64 exactly like
     * native `mpack_write_u64`.
     */
    private fun writeUnsigned(packer: MessagePacker, value: ULong) {
        if (value <= Long.MAX_VALUE.toULong()) {
            packer.packLong(value.toLong())
        } else {
            packer.packBigInteger(BigInteger(value.toString()))
        }
    }
}
