package com.ghostlock.app.data.ipsec

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.profile.ChannelBStdin
import com.ghostlock.app.data.profile.IpsecSessionSecrets
import com.ghostlock.app.data.profile.SessionSecretFrame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * IpsecSessionFactory is injectable: a host with no Android IpSecManager can
 * substitute a fake and observe the typed failure/ready contract without ever
 * building a real SA.
 */
class IpsecSessionFactoryTest {

    private fun secrets() = IpsecSessionSecrets(
        spi = 0x0a0b0c0du,
        encapPort = 4500u,
        senderPort = 5353u,
        icvLen = SessionSecretFrame.ICV_LEN,
        aesKey = ByteArray(32) { (0x10 + it).toByte() },
        hmacKey = ByteArray(32) { (0x30 + it).toByte() },
    )

    private class FakeSession(override val secrets: IpsecSessionSecrets) : IpsecSession {
        var closeCount = 0
        override fun close() {
            closeCount++
        }
    }

    @Test
    fun failureIsTypedAndDoesNotThrow() {
        val factory = IpsecSessionFactory {
            IpsecSessionResult.Failure(IpsecSessionFailureReason.PermissionDenied, "denied")
        }
        val result = factory.create()
        assertTrue(result is IpsecSessionResult.Failure)
        val failure = result as IpsecSessionResult.Failure
        assertEquals(IpsecSessionFailureReason.PermissionDenied, failure.reason)
        assertEquals("denied", failure.detail)
    }

    @Test
    fun readySessionCanBeFramedForChannelB() {
        val fake = FakeSession(secrets())
        val factory = IpsecSessionFactory { IpsecSessionResult.Ready(fake) }
        val ready = factory.create() as IpsecSessionResult.Ready
        val framed = ChannelBStdin.sessionFrame(BackendKind.Cve2026_43284, ready.session.secrets)
        assertEquals(SessionSecretFrame.LENGTH_PREFIX_SIZE + SessionSecretFrame.PAYLOAD_SIZE, framed!!.size)
        assertEquals(secrets(), ready.session.secrets)
    }

    @Test
    fun nonCve43284BackendNeverReceivesTheFrame() {
        val fake = FakeSession(secrets())
        assertNull(ChannelBStdin.sessionFrame(BackendKind.Cve2026_43499, fake.secrets))
    }
}
