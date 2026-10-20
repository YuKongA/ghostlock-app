package com.ghostlock.app.engine.pagecache.stage

import android.content.Context
import android.net.IpSecManager
import android.net.IpSecTransform
import java.net.DatagramSocket
import java.net.InetAddress
import java.security.SecureRandom

/** DFRoot 同款 raw SA：AES-CBC、无认证。 */
class RawSaSession private constructor(
    val spi: Int,
    val encapPort: Int,
    val senderPort: Int,
    val aesKey: ByteArray,
    private val transform: IpSecTransform,
    private val encap: IpSecManager.UdpEncapsulationSocket,
) : AutoCloseable {
    override fun close() {
        runCatching { transform.close() }
        runCatching { encap.close() }
        aesKey.fill(0)
    }

    companion object {
        fun open(context: Context, log: StageLog = silentStageLog): RawSaSession? {
            log.verbose("RawSaSession.open")
            return runCatching {
                val manager = context.getSystemService(IpSecManager::class.java)
                if (manager == null) {
                    log.debug("RawSaSession.open", "manager-null")
                    return null
                }
                val encap = manager.openUdpEncapsulationSocket()
                val sender = DatagramSocket()
                val senderPort = sender.localPort
                sender.close()
                val loopback = InetAddress.getByName("127.0.0.1")
                val spi = manager.allocateSecurityParameterIndex(loopback)
                val aes = ByteArray(32).also { SecureRandom().nextBytes(it) }
                val transform = IpSecTransform.Builder(context)
                    .setEncryption(android.net.IpSecAlgorithm(android.net.IpSecAlgorithm.CRYPT_AES_CBC, aes))
                    .setIpv4Encapsulation(encap, senderPort)
                    .buildTransportModeTransform(loopback, spi)
                log.debug("RawSaSession.open", "ok", "spi" to spi.spi, "encap" to encap.port,
                    "sender" to senderPort)
                RawSaSession(spi.spi, encap.port, senderPort, aes.copyOf(), transform, encap)
            }.getOrElse { failure ->
                log.debug("RawSaSession.open", "exception", "type" to failure.javaClass.simpleName)
                null
            }
        }
    }
}
