package com.ghostlock.app.data.profile

import com.ghostlock.app.data.component.BackendKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Channel-B composition tests: the session frame is appended to the app-call
 * stdin stream for cve_2026_43284 only, and a 43284 selection without secrets
 * fails closed. 43499 bytes stay exactly the length-prefixed GLKv3 document.
 */
class ChannelBStdinTest {

    private val document = byteArrayOf(
        0x82.toByte(), 0xa6.toByte(), 0x73, 0x63, 0x68, 0x65,
        0x6d, 0x61, 0x03, 0xa8.toByte(),
    )

    private fun secrets() = IpsecSessionSecrets(
        spi = 0x11223344u,
        encapPort = 4500u,
        senderPort = 12345u,
        icvLen = SessionSecretFrame.ICV_LEN,
        aesKey = ByteArray(32) { (0x40 + it).toByte() },
        hmacKey = ByteArray(32) { (0x60 + it).toByte() },
    )

    @Test
    fun only43284RequiresASessionFrame() {
        assertTrue(ChannelBStdin.requiresSessionFrame(BackendKind.Cve2026_43284))
        assertFalse(ChannelBStdin.requiresSessionFrame(BackendKind.Cve2026_43499))
        assertFalse(ChannelBStdin.requiresSessionFrame(BackendKind.Cve2026_64560))
        assertFalse(ChannelBStdin.requiresSessionFrame(null))
    }

    @Test
    fun cve43499AppCallIsTheGlkv3FrameAloneEvenWithSecrets() {
        val stream = requireNotNull(
            ChannelBStdin.appCall(BackendKind.Cve2026_43499, document, secrets()),
        )
        val expected = SessionSecretFrame.lengthPrefix(document.size) + document
        assertArrayEquals(expected, stream)
        assertEquals(4 + document.size, stream.size)
    }

    @Test
    fun cve43284AppCallAppendsTheFramedSessionFrameAfterGlkv3() {
        val stream = requireNotNull(
            ChannelBStdin.appCall(BackendKind.Cve2026_43284, document, secrets()),
        )
        val expected = SessionSecretFrame.appCallStdin(document, secrets())
        assertArrayEquals(expected, stream)
        assertEquals(
            4 + document.size + 4 + SessionSecretFrame.PAYLOAD_SIZE,
            stream.size,
        )
    }

    @Test
    fun cve43284AppCallWithoutSecretsFailsClosed() {
        assertNull(ChannelBStdin.appCall(BackendKind.Cve2026_43284, document, null))
    }

    @Test
    fun sessionFrameIsNullForEveryBackendExceptCve43284() {
        assertNull(ChannelBStdin.sessionFrame(BackendKind.Cve2026_43499, secrets()))
        assertNull(ChannelBStdin.sessionFrame(BackendKind.Cve2026_64560, secrets()))
        assertNull(ChannelBStdin.sessionFrame(null, secrets()))
    }

    @Test
    fun sessionFrameForCve43284IsTheFramedEncoding() {
        val framed = requireNotNull(
            ChannelBStdin.sessionFrame(BackendKind.Cve2026_43284, secrets()),
        )
        assertArrayEquals(SessionSecretFrame.encodeFramed(secrets()), framed)
        assertNull(ChannelBStdin.sessionFrame(BackendKind.Cve2026_43284, null))
    }
}
