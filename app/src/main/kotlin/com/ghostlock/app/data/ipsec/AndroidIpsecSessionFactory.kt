package com.ghostlock.app.data.ipsec

import android.content.Context
import android.net.IpSecAlgorithm
import android.net.IpSecManager
import android.net.IpSecTransform
import com.ghostlock.app.data.profile.IpsecSessionSecrets
import com.ghostlock.app.data.profile.SessionSecretFrame
import java.net.DatagramSocket
import java.net.InetAddress
import java.security.SecureRandom
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Android IpSecManager-backed SA factory, mirroring the upstream reference
 * (third_party/dirtyfrag/app-reference/ExploitRunner.java):
 *
 *  - keys are generated here with [SecureRandom] and handed to both the
 *    transform and native, so the kernel never needs to disclose them;
 *  - [IpSecAlgorithm.CRYPT_AES_CBC] + [IpSecAlgorithm.AUTH_HMAC_SHA256]
 *    truncated to [SessionSecretFrame.ICV_LEN] bytes, matching the native
 *    ipsec/ parameter block;
 *  - the caller must close the returned [IpsecSession] after native exits so
 *    the transform and encap socket stay installed for the whole run.
 *
 * Every platform failure is mapped to an [IpsecSessionResult.Failure]; the
 * caller logs the reason without key bytes. Key arrays are wiped after the
 * transform is built.
 */
class AndroidIpsecSessionFactory(private val context: Context) : IpsecSessionFactory {

    override fun create(): IpsecSessionResult {
        val ipsec = context.getSystemService(Context.IPSEC_SERVICE) as? IpSecManager
            ?: return IpsecSessionResult.Failure(
                IpsecSessionFailureReason.Unavailable,
                "IpSecManager unavailable (requires API 28+)",
            )

        var encap: IpSecManager.UdpEncapsulationSocket? = null
        var spi: IpSecManager.SecurityParameterIndex? = null
        var transform: IpSecTransform? = null
        var aesKey: ByteArray? = null
        var hmacKey: ByteArray? = null
        return try {
            val loopback = InetAddress.getByName(LOOPBACK_ADDRESS)
            val encapSocket = ipsec.openUdpEncapsulationSocket()
            encap = encapSocket
            val spiReservation = ipsec.allocateSecurityParameterIndex(loopback)
            spi = spiReservation

            val random = SecureRandom()
            val aes = ByteArray(SessionSecretFrame.KEY_BYTES).also(random::nextBytes)
            val hmac = ByteArray(SessionSecretFrame.KEY_BYTES).also(random::nextBytes)
            aesKey = aes
            hmacKey = hmac

            /* A throwaway sender socket: native binds the same local port for
             * the ESP-in-UDP SA. */
            val senderPort = DatagramSocket().use { it.localPort }

            val builder = IpSecTransform.Builder(context)
                .setEncryption(IpSecAlgorithm(IpSecAlgorithm.CRYPT_AES_CBC, aes))
                .setAuthentication(
                    IpSecAlgorithm(
                        IpSecAlgorithm.AUTH_HMAC_SHA256,
                        hmac,
                        SessionSecretFrame.ICV_LEN.toInt() * Byte.SIZE_BITS,
                    ),
                )
                .setIpv4Encapsulation(encapSocket, senderPort)
            val built = builder.buildTransportModeTransform(loopback, spiReservation)
            transform = built

            val secrets = IpsecSessionSecrets(
                spi = spiReservation.spi.toUInt(),
                encapPort = encapSocket.port.toUShort(),
                senderPort = senderPort.toUShort(),
                icvLen = SessionSecretFrame.ICV_LEN,
                aesKey = aes,
                hmacKey = hmac,
            )
            IpsecSessionResult.Ready(
                AndroidIpsecSession(secrets, built, spiReservation, encapSocket),
            )
        } catch (error: SecurityException) {
            closeQuietly(transform, spi, encap)
            IpsecSessionResult.Failure(IpsecSessionFailureReason.PermissionDenied, error.message)
        } catch (error: Exception) {
            closeQuietly(transform, spi, encap)
            IpsecSessionResult.Failure(IpsecSessionFailureReason.BuildFailed, error.message)
        } finally {
            aesKey?.fill(0)
            hmacKey?.fill(0)
        }
    }

    private companion object {
        const val LOOPBACK_ADDRESS = "127.0.0.1"

        fun closeQuietly(
            transform: IpSecTransform?,
            spi: IpSecManager.SecurityParameterIndex?,
            encap: IpSecManager.UdpEncapsulationSocket?,
        ) {
            runCatching { transform?.close() }
            runCatching { spi?.close() }
            runCatching { encap?.close() }
        }
    }
}

/** Resource-owning handle returned by [AndroidIpsecSessionFactory]. */
private class AndroidIpsecSession(
    override val secrets: IpsecSessionSecrets,
    private val transform: IpSecTransform,
    private val spi: IpSecManager.SecurityParameterIndex,
    private val encap: IpSecManager.UdpEncapsulationSocket,
) : IpsecSession {
    private val closed = AtomicBoolean(false)

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        runCatching { transform.close() }
        runCatching { spi.close() }
        runCatching { encap.close() }
    }
}
