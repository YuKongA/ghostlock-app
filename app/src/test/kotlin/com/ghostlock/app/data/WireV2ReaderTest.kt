package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream

/**
 * v2 framing guards. These are READER-level cases (format + presence); the
 * mapping-level negatives (unknown section/key, unknown selection id, negative
 * stays negative, …) belong to the converter test.
 */
class WireV2ReaderTest {

    /** Test-only little-endian v2 writer (the product has no v2 writer). */
    private class V2(
        val terminal: Int = 1,
        val backend: Int = 1,
        val middleware: Int = 0x0001,
        val release: String = "5.15.189-android13-8-00016-g51bba4309aac",
        val sections: List<Pair<String, List<Pair<String, ULong>>>> = emptyList(),
        val truncate: Int = 0,
        val trailing: Int = 0,
    ) {
        fun bytes(): ByteArray {
            val out = ByteArrayOutputStream()
            fun le(value: Long, width: Int) {
                for (index in 0 until width) out.write(((value shr (8 * index)) and 0xFF).toInt())
            }
            le(WireV2Reader.MAGIC.toLong(), 4)
            le(2L, 2)
            le(terminal.toLong(), 2)
            le(backend.toLong(), 2)
            le(middleware.toLong(), 2)
            val releaseBytes = release.toByteArray(Charsets.UTF_8)
            le(releaseBytes.size.toLong(), 2)
            le(0L, 2)
            out.write(releaseBytes)
            le(sections.size.toLong(), 2)
            for ((name, entries) in sections) {
                out.write(name.toByteArray(Charsets.UTF_8).size)
                out.write(name.toByteArray(Charsets.UTF_8))
                le(entries.size.toLong(), 4)
                for ((key, raw) in entries) {
                    out.write(key.toByteArray(Charsets.UTF_8).size)
                    out.write(key.toByteArray(Charsets.UTF_8))
                    le(raw.toLong(), 8)
                }
            }
            var result = out.toByteArray()
            if (truncate > 0) result = result.copyOf(result.size - truncate)
            if (trailing > 0) result += ByteArray(trailing)
            return result
        }
    }

    private fun rejects(bytes: ByteArray, fragment: String) {
        val error = assertThrows(IllegalArgumentException::class.java) { WireV2Reader.read(bytes) }
        assertTrue("error must mention '$fragment': " + error.message, error.message.orEmpty().contains(fragment))
    }

    @Test
    fun `a well-formed document frames, and presence is entry occurrence`() {
        val document = WireV2Reader.read(
            V2(
                sections = listOf(
                    "meta" to listOf("kernel_major" to 6uL, "safe_mode" to 0uL),
                    "offset" to listOf("slide_boot_id" to 0xFFFFFFFFFFFFFFFFuL),
                ),
            ).bytes(),
        )
        assertEquals(1, document.terminalId)
        assertEquals(1, document.backendId)
        assertEquals(1, document.routeId)
        assertEquals("5.15.189-android13-8-00016-g51bba4309aac", document.release)
        assertEquals(listOf("meta", "offset"), document.sections.map { it.name })
        val meta = document.sections.first { it.name == "meta" }.entries
        assertEquals(listOf("kernel_major", "safe_mode"), meta.map { it.key })
        /* Written 0 is PRESENT (and stays 0); absence is the only "not written". */
        assertEquals(0uL, meta.first { it.key == "safe_mode" }.raw)
        assertEquals(
            0xFFFFFFFFFFFFFFFFuL,
            document.sections.first { it.name == "offset" }.entries.single().raw,
        )
        assertNull(meta.firstOrNull { it.key == "kernel_minor" })
    }

    @Test
    fun `an empty section list is legal`() {
        assertEquals(emptyList<String>(), WireV2Reader.read(V2().bytes()).sections.map { it.name })
    }

    @Test
    fun `the 43284 backend may carry Auto as "no route"`() {
        val document = WireV2Reader.read(V2(backend = 6, middleware = 0x0000).bytes())
        assertEquals(0, document.routeId)
        /* …but every other backend must carry a real route. */
        rejects(V2(backend = 1, middleware = 0x0000).bytes(), "route 0")
    }

    @Test
    fun `header guards for size magic version ids and middleware`() {
        rejects(ByteArray(8), "truncated header")
        rejects(V2().bytes().also { it[0] = 0x22 }, "bad magic")
        rejects(V2().bytes().also { it[4] = 3 }, "unsupported version")
        rejects(V2(terminal = 9).bytes(), "unknown terminal id 9")
        rejects(V2(backend = 7).bytes(), "unknown backend id 7")
        rejects(V2(middleware = 0x0101).bytes(), "exceeds 0xff")
        rejects(V2(middleware = 0x0009).bytes(), "does not resolve")
    }

    @Test
    fun `release and body bounds fail closed`() {
        /* A release length past the document. */
        val overlong = V2().bytes().also { it[12] = 0xFF.toByte(); it[13] = 0xFF.toByte() }
        rejects(overlong, "release length")
        rejects(V2(truncate = 1).bytes(), "v2:")
        rejects(V2(trailing = 3).bytes(), "trailing byte")
        /* Truncated in the middle of a section header / entry. */
        val one = V2(sections = listOf("meta" to listOf("kernel_major" to 6uL)))
        rejects(one.bytes().copyOf(one.bytes().size - 6), "v2:")
        val emptyName = ByteArrayOutputStream().also { out ->
            val base = V2(sections = listOf("meta" to emptyList())).bytes()
            out.write(base)
        }
        rejects(emptyName.toByteArray().copyOf(emptyName.size() - 6) + byteArrayOf(0, 0), "v2:")
    }
}
