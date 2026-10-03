package com.ghostlock.app.data.profile

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * B5-1 session-secret frame tests (channel B).
 *
 * The golden payload hex below is asserted byte-for-byte by the native
 * `core/tests/session_frame_test.cpp` as well, so a divergence in field order,
 * width or endianness between Kotlin and native fails on one side.
 */
class SessionSecretFrameTest {

    private fun sample() = IpsecSessionSecrets(
        spi = 0x01020304u,
        encapPort = 0x0506u,
        senderPort = 0x0708u,
        icvLen = 16u,
        aesKey = ByteArray(32) { it.toByte() },
        hmacKey = ByteArray(32) { (0x20 + it).toByte() },
    )

    private fun hex(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }

    /** Native session_frame_test.cpp's kGoldenHex. */
    private val goldenHex =
        "01010000010203040506070810000000" +
            "000102030405060708090a0b0c0d0e0f" +
            "101112131415161718191a1b1c1d1e1f" +
            "202122232425262728292a2b2c2d2e2f" +
            "303132333435363738393a3b3c3d3e3f" +
            "34383238"

    @Test
    fun payloadMatchesNativeGolden() {
        val encoded = SessionSecretFrame.encode(sample())
        assertEquals(SessionSecretFrame.PAYLOAD_SIZE, encoded.size)
        assertEquals(goldenHex, hex(encoded))
    }

    @Test
    fun multiByteFieldsAreBigEndian() {
        val encoded = SessionSecretFrame.encode(sample())
        /* spi +0x04, encap_port +0x08, sender_port +0x0a. */
        assertArrayEquals(
            byteArrayOf(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
            encoded.copyOfRange(4, 12),
        )
        /* trailer +0x50. */
        assertArrayEquals(
            byteArrayOf(0x34, 0x38, 0x32, 0x38),
            encoded.copyOfRange(80, 84),
        )
    }

    @Test
    fun framedAddsBigEndianLengthPrefix() {
        val framed = SessionSecretFrame.encodeFramed(sample())
        assertEquals(4 + SessionSecretFrame.PAYLOAD_SIZE, framed.size)
        assertArrayEquals(
            byteArrayOf(0x00, 0x00, 0x00, 0x54),
            framed.copyOfRange(0, 4),
        )
        assertArrayEquals(SessionSecretFrame.encode(sample()), framed.copyOfRange(4, framed.size))
    }

    @Test
    fun appCallStdinIsGlkv3FrameThenSessionFrame() {
        val document = byteArrayOf(
            0x82.toByte(), 0xa6.toByte(), 0x73, 0x63, 0x68, 0x65,
            0x6d, 0x61, 0x03, 0xa8.toByte(),
        )
        val secrets = sample()
        val stream = SessionSecretFrame.appCallStdin(document, secrets)
        val expected =
            SessionSecretFrame.lengthPrefix(document.size) + document +
                SessionSecretFrame.encodeFramed(secrets)
        assertArrayEquals(expected, stream)
        /* The GLKv3 bytes are untouched: no re-encoding. */
        assertArrayEquals(
            document,
            stream.copyOfRange(4, 4 + document.size),
        )
    }

    @Test
    fun lengthPrefixIsBigEndian() {
        assertArrayEquals(
            byteArrayOf(0x00, 0x00, 0x00, 0x54),
            SessionSecretFrame.lengthPrefix(84),
        )
        assertArrayEquals(
            byteArrayOf(0x01, 0x00, 0x00, 0x00),
            SessionSecretFrame.lengthPrefix(0x01000000),
        )
    }

    @Test
    fun keyWidthIsEnforced() {
        var threw = false
        try {
            IpsecSessionSecrets(
                spi = 1u,
                encapPort = 1u,
                senderPort = 1u,
                aesKey = ByteArray(31),
                hmacKey = ByteArray(32),
            )
        } catch (_: IllegalArgumentException) {
            threw = true
        }
        assertTrue(threw)
    }

    @Test
    fun keyArraysAreDefensivelyCopied() {
        val aes = ByteArray(32) { 0x11 }
        val hmac = ByteArray(32) { 0x22 }
        val secrets = IpsecSessionSecrets(1u, 1u, 1u, 16u, aes, hmac)
        aes[0] = 0x00
        hmac[0] = 0x00
        assertEquals(0x11, secrets.aesKey[0].toInt() and 0xff)
        assertEquals(0x22, secrets.hmacKey[0].toInt() and 0xff)
    }
}
