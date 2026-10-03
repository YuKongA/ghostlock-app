package com.ghostlock.app.data.profile

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * GLKv3-4 decoder and safe-mode patch tests. The decoder is the inverse of
 * [Glkv3Encoder] for the wire value subset; [Glkv3Decoder.patchSafeMode] is the
 * v3 replacement for the v2 byte scan.
 */
class Glkv3DecoderTest {
    private val sample = Glkv3Document(
        release = "5.15.189-android13-8-00016-g51bba4309aac",
        terminal = "root_child",
        backend = "cve_2026_43499",
        route = "multicast_waiter",
        sections = listOf(
            Glkv3Section(
                "meta",
                listOf(
                    Glkv3Entry("kernel_major", Glkv3Value.UInt(5u)),
                    Glkv3Entry("safe_mode", Glkv3Value.Bool(false)),
                ),
            ),
            Glkv3Section(
                "offset",
                listOf(
                    Glkv3Entry("init_task", Glkv3Value.UInt(34677760u)),
                    Glkv3Entry("slide", Glkv3Value.Int(-2)),
                ),
            ),
        ),
    )

    @Test
    fun roundTripsTheCanonicalEncoder() {
        val encoded = Glkv3Encoder.encode(sample)
        val decoded = requireNotNull(Glkv3Decoder.decode(encoded))
        assertEquals(sample.schema, decoded.schema)
        assertEquals(sample.release, decoded.release)
        assertEquals(sample.terminal, decoded.terminal)
        assertEquals(sample.backend, decoded.backend)
        assertEquals(sample.route, decoded.route)
        assertEquals(Glkv3Value.UInt(5u), valueOf(decoded, "meta", "kernel_major"))
        assertEquals(Glkv3Value.Bool(false), valueOf(decoded, "meta", "safe_mode"))
        assertEquals(Glkv3Value.Int(-2), valueOf(decoded, "offset", "slide"))
        assertArrayEquals(encoded, Glkv3Encoder.encode(decoded))
    }

    @Test
    fun rejectsNonCanonicalDocuments() {
        assertNull(Glkv3Decoder.decode(ByteArray(0)))
        assertNull(Glkv3Decoder.decode(byteArrayOf(0x01)))
        assertNull(Glkv3Decoder.decode(Glkv3Encoder.encode(sample.copy(schema = 2uL))))
        assertNull(Glkv3Decoder.decode(byteArrayOf(0x81.toByte(), 0xa1.toByte(), 0x78.toByte())))
    }

    @Test
    fun patchSafeModeFlipsOnlySafeMode() {
        val encoded = Glkv3Encoder.encode(sample)
        val patched = requireNotNull(Glkv3Decoder.patchSafeMode(encoded))
        val decoded = requireNotNull(Glkv3Decoder.decode(patched))
        assertEquals(Glkv3Value.Bool(true), valueOf(decoded, "meta", "safe_mode"))
        assertEquals(sample.release, decoded.release)
        assertEquals(sample.terminal, decoded.terminal)
        assertEquals(sample.backend, decoded.backend)
        assertEquals(sample.route, decoded.route)
        assertEquals(sample.sections.size, decoded.sections.size)
        assertEquals(Glkv3Value.Int(-2), valueOf(decoded, "offset", "slide"))
        assertNull(Glkv3Decoder.patchSafeMode(byteArrayOf(0x01)))
    }

    private fun valueOf(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections.first { it.name == section }
            .entries.first { it.key == key }
            .value
}
