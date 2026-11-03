package com.ghostlock.app.backend.cve43284glued

import android.content.Context
import android.net.IpSecAlgorithm
import android.net.IpSecManager
import android.net.IpSecTransform
import java.net.DatagramSocket
import java.net.InetAddress
import java.security.SecureRandom
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Owns the per-run Android IPsec SA and the exact CLI arguments consumed by upstream DFRoot.
 *
 * The runtime is created in the process domain that will launch the ELF (App or Shizuku shell),
 * remains alive until that ELF exits, and releases transform, SPI, then encapsulation socket.
 * The AES key is kept in memory only and is cleared on close.
 */
internal class AndroidIpSecRuntime private constructor(
    arguments: List<String>,
    private val aesKey: ByteArray,
    private val resources: ReverseCloseScope,
) : AutoCloseable {
    private val closed = AtomicBoolean(false)
    private val argumentTokens = arguments.toMutableList()

    /** @return a run-scoped copy of the upstream argv while this runtime remains open. */
    val arguments: List<String>
        get() = synchronized(this) { argumentTokens.toList() }

    /**
     * Releases this run's IPsec resources once, in reverse acquisition order, and clears key bytes.
     *
     * @throws IllegalStateException when one or more Android resource close operations fail.
     */
    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        val failures = resources.close()
        aesKey.fill(0)
        synchronized(this) { argumentTokens.clear() }
        if (failures.isNotEmpty()) {
            throw IllegalStateException("exec:ipsec:resource-close:${failures.joinToString(",")}")
        }
    }

    internal companion object {
        /**
         * Creates the DFRoot-compatible loopback transform and runtime argument vector.
         *
         * @param context Context of the selected launcher process; the service is never borrowed
         * from the other process domain.
         * @return a live runtime owner; the caller must close it after child termination.
         * @throws Exception when the IPsec service or any required resource cannot be acquired.
         * @note Called during the prepare/exec phase before the vulnerable ELF is started.
         */
        fun create(context: Context): AndroidIpSecRuntime {
            val manager = context.getSystemService(Context.IPSEC_SERVICE) as? IpSecManager
                ?: throw IllegalStateException("IpSecManager unavailable")
            val loopback = InetAddress.getByName("127.0.0.1")
            val resources = ReverseCloseScope()
            val key = ByteArray(AES_KEY_BYTES)
            try {
                val encapSocket = manager.openUdpEncapsulationSocket()
                resources.add("encapsulation") { encapSocket.close() }

                val spi = manager.allocateSecurityParameterIndex(loopback)
                resources.add("spi") { spi.close() }

                SecureRandom().nextBytes(key)
                val senderPort = DatagramSocket().use { socket -> socket.localPort }
                val transform = IpSecTransform.Builder(context)
                    .setEncryption(IpSecAlgorithm(IpSecAlgorithm.CRYPT_AES_CBC, key))
                    .setIpv4Encapsulation(encapSocket, senderPort)
                    .buildTransportModeTransform(loopback, spi)
                resources.add("transform") { transform.close() }

                val arguments = IpSecCliArguments.create(
                    encapPort = encapSocket.port,
                    senderPort = senderPort,
                    spi = spi.spi,
                    aesKey = key,
                )
                return AndroidIpSecRuntime(arguments, key, resources)
            } catch (problem: Exception) {
                key.fill(0)
                resources.close()
                throw problem
            }
        }

        private const val AES_KEY_BYTES = 32
    }
}

/**
 * Builds a bounded argv vector matching the DFRoot C parser without shell interpolation.
 *
 * @note The key is encoded only for the upstream executable argument contract; callers must not
 * persist, log, or send the returned vector over Binder.
 */
internal object IpSecCliArguments {
    /**
     * Validates and formats the four upstream options in their parser order.
     *
     * @param encapPort UDP encapsulation socket port.
     * @param senderPort temporary local UDP port selected by the launcher domain.
     * @param spi unsigned SPI bit pattern returned by IpSecManager.
     * @param aesKey 32-byte AES-CBC key held by the live transform.
     * @return four fixed option/value pairs as individual argv tokens.
     * @throws IllegalArgumentException when a port, SPI, or key length is invalid.
     * @note This formatter runs immediately before process start; it creates no files or logs.
     */
    fun create(encapPort: Int, senderPort: Int, spi: Int, aesKey: ByteArray): List<String> {
        require(encapPort in 1..MAX_PORT) { "encapsulation port out of range" }
        require(senderPort in 1..MAX_PORT) { "sender port out of range" }
        require(spi != 0) { "SPI must be nonzero" }
        require(aesKey.size == AES_KEY_BYTES) { "AES key must be 32 bytes" }
        return listOf(
            "--encap-port", encapPort.toString(),
            "--sender-port", senderPort.toString(),
            "--spi", java.lang.Integer.toUnsignedString(spi),
            "--aes-key", aesKey.toLowerHex(),
        )
    }

    private fun ByteArray.toLowerHex(): String = buildString(size * 2) {
        for (byte in this@toLowerHex) {
            val value = byte.toInt() and 0xff
            append(HEX[value ushr 4])
            append(HEX[value and 0x0f])
        }
    }

    private const val MAX_PORT = 65535
    private const val AES_KEY_BYTES = 32
    private const val HEX = "0123456789abcdef"
}

/**
 * Tracks acquired resources and releases each at most once in reverse acquisition order.
 *
 * @note Cleanup attempts continue after an individual close failure so one error cannot strand
 * a later resource in the ownership stack.
 */
internal class ReverseCloseScope {
    private val actions = mutableListOf<Pair<String, () -> Unit>>()
    private var closed = false

    /**
     * Adds one resource release action to this run's owner.
     *
     * @param name stable non-secret resource label used only in cleanup diagnostics.
     * @param action close operation invoked during reverse teardown.
     * @throws IllegalStateException when ownership was already terminated.
     */
    @Synchronized
    fun add(name: String, action: () -> Unit) {
        check(!closed) { "resource scope already closed" }
        actions += name to action
    }

    /**
     * Runs all close actions once and returns labels for failures without exposing secret data.
     *
     * @return failed resource labels in reverse acquisition order, or an empty list.
     */
    fun close(): List<String> {
        val pending = synchronized(this) {
            if (closed) return emptyList()
            closed = true
            actions.asReversed().toList().also { actions.clear() }
        }
        return pending.mapNotNull { (name, action) ->
            runCatching(action).exceptionOrNull()?.let { "$name:${it::class.java.simpleName}" }
        }
    }
}
