package com.ghostlock.app.data.profile

import org.msgpack.core.MessagePack
import org.msgpack.value.ArrayValue
import org.msgpack.value.BinaryValue
import org.msgpack.value.BooleanValue
import org.msgpack.value.IntegerValue
import org.msgpack.value.MapValue
import org.msgpack.value.StringValue
import org.msgpack.value.Value

/**
 * Strict GLKv3 (MessagePack) reader (GLKv3-4).
 *
 * It is the inverse of [Glkv3Encoder] for the value subset the wire model
 * allows: a map-rooted document with schema == 3, optional string selection
 * keys and a sections map of string-keyed maps holding only uint/int/bool/
 * str/bin/array values. Any other shape (non-map root, wrong schema, a nested
 * map where a scalar is expected, nil/float/extension) returns null, so the
 * caller can fail closed.
 *
 * The reader is used by the v3 safe-mode patch and by tests that need to read
 * a document back; production native parsing is done by MPack.
 */
object Glkv3Decoder {
    /** Decodes [bytes] or returns null when the document is not canonical GLKv3. */
    fun decode(bytes: ByteArray): Glkv3Document? {
        if (bytes.isEmpty()) return null
        return try {
            MessagePack.newDefaultUnpacker(bytes).use { unpacker ->
                val root = unpacker.unpackValue()
                if (unpacker.hasNext()) return null
                val map = root as? MapValue ?: return null
                var schema: ULong? = null
                var release: String? = null
                var terminal: String? = null
                var backend: String? = null
                var route: String? = null
                var sections: List<Glkv3Section>? = null
                for ((keyValue, value) in map.map()) {
                    when (val key = keyValue.asStringValue().asString()) {
                        "schema" -> schema = (value as? IntegerValue)?.asBigInteger()?.let {
                            if (it.signum() < 0 || it.bitLength() > 64) null else it.toLong().toULong()
                        }
                        "release" -> release = (value as? StringValue)?.asString()
                        "terminal" -> terminal = (value as? StringValue)?.asString()
                        "backend" -> backend = (value as? StringValue)?.asString()
                        "route" -> route = (value as? StringValue)?.asString()
                        "sections" -> sections = decodeSections(value)
                        else -> return null
                    }
                }
                if (schema != Glkv3Encoder.SCHEMA_VERSION) return null
                if (release == null || terminal == null || backend == null) return null
                if (sections == null) return null
                Glkv3Document(
                    schema = schema,
                    release = release,
                    terminal = terminal,
                    backend = backend,
                    route = route,
                    sections = sections,
                )
            }
        } catch (_: Exception) {
            null
        }
    }

    /**
     * Returns a canonical copy of a GLKv3 [document] with common.safe_mode set to
     * true, or null when [document] is not a well-formed GLKv3 document.
     */
    fun patchSafeMode(document: ByteArray): ByteArray? {
        val decoded = decode(document) ?: return null
        val patched = decoded.sections.map { section ->
            if (section.name != "common") {
                section
            } else {
                Glkv3Section(
                    name = "common",
                    entries = section.entries.filterNot { it.key == "safe_mode" } +
                        Glkv3Entry("safe_mode", Glkv3Value.Bool(true)),
                )
            }
        }
        val withCommon = if (patched.any { it.name == "common" }) {
            patched
        } else {
            patched + Glkv3Section("common", listOf(Glkv3Entry("safe_mode", Glkv3Value.Bool(true))))
        }
        return Glkv3Encoder.encode(decoded.copy(sections = withCommon))
    }

    private fun decodeSections(value: Value): List<Glkv3Section>? {
        val map = value as? MapValue ?: return null
        return map.map().map { (nameValue, entriesValue) ->
            val name = (nameValue as? StringValue)?.asString() ?: return null
            val entries = entriesValue as? MapValue ?: return null
            Glkv3Section(
                name = name,
                entries = entries.map().map { (keyValue, entryValue) ->
                    val key = (keyValue as? StringValue)?.asString() ?: return null
                    Glkv3Entry(key, decodeValue(entryValue) ?: return null)
                },
            )
        }
    }

    private fun decodeValue(value: Value): Glkv3Value? = when (value) {
        is BooleanValue -> Glkv3Value.Bool(value.boolean)
        is IntegerValue -> {
            val big = value.asBigInteger()
            if (big.signum() < 0) Glkv3Value.Int(value.asLong())
            else Glkv3Value.UInt(big.toLong().toULong())
        }
        is StringValue -> Glkv3Value.Str(value.asString())
        is BinaryValue -> Glkv3Value.Bin(value.asByteArray())
        is ArrayValue -> Glkv3Value.Array(
            value.list().map { element -> decodeValue(element) ?: return null },
        )
        else -> null
    }
}
