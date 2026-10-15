package com.ghostlock.app.data.nativeaccess

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Kotlin half of the wire, checked against the native half.
 *
 * The tables here are typed mirrors of core/src/jni/session_protocol.hpp; the test compares all
 * three of them against the export the native side produces (build/native/jni-protocol-manifest.tsv,
 * copied into app/build by the exportJniProtocolManifest task). A rename or renumber on either side
 * fails here instead of on a device, which is the whole point of exporting the list.
 */
class NativeProtocolTest {
    private fun manifestLines(): List<Triple<String, String, Int>> {
        val candidates = listOf(
            File("build/jni-protocol-manifest.tsv"),
            File("../build/native/jni-protocol-manifest.tsv"),
        )
        val file = candidates.firstOrNull { it.isFile }
        assertNotNull(
            "no protocol manifest; run ./gradlew exportJniProtocolManifest first (tried " +
                candidates.joinToString { it.path } + ")",
            file,
        )
        return file!!.readLines()
            .filter { it.isNotBlank() && !it.startsWith("#") }
            .mapNotNull { line ->
                val parts = line.split('\t')
                if (parts.size != 3) {
                    null
                } else {
                    Triple(parts[0], parts[1], parts[2].toInt())
                }
            }
    }

    @Test
    fun operationTableMatchesTheNativeExport() {
        val exported = manifestLines().filter { it.first == "op" }.associate { it.second to it.third }
        assertEquals("op count", NativeProtocol.Op.entries.size, exported.size)
        for (op in NativeProtocol.Op.entries) {
            assertEquals("op " + op.token, op.code, exported[op.token])
        }
    }

    @Test
    fun keyTableMatchesTheNativeExport() {
        val exported = manifestLines().filter { it.first == "key" }.associate { it.second to it.third }
        assertEquals("key count", NativeProtocol.Key.entries.size, exported.size)
        for (key in NativeProtocol.Key.entries) {
            assertEquals("key " + key.token, key.code, exported[key.token])
        }
    }

    @Test
    fun statusTableMatchesTheNativeExport() {
        val exported = manifestLines().filter { it.first == "status" }.associate { it.second to it.third }
        assertEquals("status count", NativeProtocol.Status.entries.size, exported.size)
        for (status in NativeProtocol.Status.entries) {
            assertEquals("status " + status.token, status.code, exported[status.token])
        }
    }

    @Test
    fun aFrameSurvivesItsOwnRoundTrip() {
        val frame = NativeProtocol.Builder(NativeProtocol.Op.WriteWord)
            .u64(NativeProtocol.Key.Address, 0xffffff8042abcdefuL)
            .u64(NativeProtocol.Key.Form, 1uL)
            .bytes(NativeProtocol.Key.Text, "hello".toByteArray())
            .build()
        val reply = NativeProtocol.parse(withStatus(frame, NativeProtocol.Status.NotReady.code))
        assertEquals(NativeProtocol.Status.NotReady, reply.status)
        assertEquals(0xffffff8042abcdefuL, reply.u64(NativeProtocol.Key.Address))
        assertEquals(1uL, reply.u64(NativeProtocol.Key.Form))
        assertEquals("hello", reply.blob(NativeProtocol.Key.Text)?.toString(Charsets.UTF_8))
        assertEquals("hello", reply.refusal)
    }

    @Test
    fun malformedFramesAreRefusedByName() {
        val good = withStatus(
            NativeProtocol.Builder(NativeProtocol.Op.Snapshot).u64(NativeProtocol.Key.Phase, 1uL).build(),
            NativeProtocol.Status.Ok.code,
        )
        assertTrue(parseFails(good.copyOf(good.size - 1), "truncated"))
        val badMagic = good.copyOf()
        badMagic[0] = 0
        assertTrue(parseFails(badMagic, "bad magic"))
        val trailing = good.copyOf(good.size + 4)
        assertTrue(parseFails(trailing, "trailing bytes"))
    }

    @Test
    fun anUnknownStatusIsReportedAsSuchNotAsSuccess() {
        val frame = withStatus(
            NativeProtocol.Builder(NativeProtocol.Op.Snapshot).build(),
            -999,
        )
        val reply = NativeProtocol.parse(frame)
        assertEquals(-999, reply.unknownStatus)
        assertTrue("an unknown status must not read as ok", !reply.status.let { it == NativeProtocol.Status.Ok })
    }

    @Test
    fun aSensitiveFrameCanBeTakenExactlyOnce() {
        val secret = byteArrayOf(1, 2, 3)
        val frame = SensitiveFrame(secret)
        assertTrue("the caller array is cleared", secret.all { it == 0.toByte() })
        assertTrue(frame.toString().contains("redacted"))
        val taken = frame.consume()
        assertEquals(3, taken.size)
        assertTrue(frame.consumed())
        var refused = false
        try {
            frame.consume()
        } catch (expected: IllegalStateException) {
            refused = true
        }
        assertTrue("a second consume is refused", refused)
    }

    /** Put a status into a request frame, so a reply can be parsed from the same bytes. */
    private fun withStatus(frame: ByteArray, status: Int): ByteArray {
        for (shift in 0..24 step 8) {
            frame[8 + shift / 8] = ((status shr shift) and 0xFF).toByte()
        }
        return frame
    }

    private fun parseFails(frame: ByteArray, what: String): Boolean = try {
        NativeProtocol.parse(frame)
        false
    } catch (expected: NativeProtocol.DecodeException) {
        true
    }
}
