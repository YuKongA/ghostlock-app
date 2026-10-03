package com.ghostlock.app.data.profile

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Runtime IpSecManager session secrets (native
 * `backend::cve_2026_43284::IpsecSaParams`).
 *
 * These are session secrets produced by the App's IpSecManager. They are carried
 * over stdin, never through the profile document, a serialized profile, the
 * process argv or a log. The key arrays are copied on construction so a caller
 * cannot mutate them afterwards.
 */
class IpsecSessionSecrets(
    val spi: UInt,
    val encapPort: UShort,
    val senderPort: UShort,
    val icvLen: UByte = SessionSecretFrame.ICV_LEN,
    aesKey: ByteArray = ByteArray(32),
    hmacKey: ByteArray = ByteArray(32),
) {
    val aesKey: ByteArray = aesKey.copyOf()
    val hmacKey: ByteArray = hmacKey.copyOf()

    init {
        require(this.aesKey.size == SessionSecretFrame.KEY_BYTES) {
            "aes_key must be ${SessionSecretFrame.KEY_BYTES} bytes, got ${this.aesKey.size}"
        }
        require(this.hmacKey.size == SessionSecretFrame.KEY_BYTES) {
            "hmac_key must be ${SessionSecretFrame.KEY_BYTES} bytes, got ${this.hmacKey.size}"
        }
    }

    override fun equals(other: Any?): Boolean =
        other is IpsecSessionSecrets &&
            spi == other.spi &&
            encapPort == other.encapPort &&
            senderPort == other.senderPort &&
            icvLen == other.icvLen &&
            aesKey.contentEquals(other.aesKey) &&
            hmacKey.contentEquals(other.hmacKey)

    override fun hashCode(): Int {
        var result = spi.hashCode()
        result = 31 * result + encapPort.hashCode()
        result = 31 * result + senderPort.hashCode()
        result = 31 * result + icvLen.hashCode()
        result = 31 * result + aesKey.contentHashCode()
        result = 31 * result + hmacKey.contentHashCode()
        return result
    }
}

/**
 * B5-1 runtime session-secret frame encoder (channel B), byte-compatible with
 * native `backend/cve_2026_43284/session_frame.cpp`.
 *
 * The `--ghostlock-app-call --enable-status-record` stdin stream is
 * `[u32 be len][GLKv3 document]` and, for the cve_2026_43284 backend only, MAY
 * be followed by `[u32 be len][session secret payload]`. See
 * docs/analysis/cve-2026-43284-refactor-plan.md section 6.4.
 *
 * Frozen 84-byte payload layout (all multi-byte fields big-endian):
 * ```
 *   +0  u8    version        = 1
 *   +1  u8    kind           = 1      (IPSEC_SA)
 *   +2  u16 be reserved      = 0
 *   +4  u32 be spi
 *   +8  u16 be encap_port
 *   +10 u16 be sender_port
 *   +12 u8    icv_len        = 16
 *   +13 u8    reserved2[3]   = 0
 *   +16 u8    aes_key[32]
 *   +48 u8    hmac_key[32]
 *   +80 u32 be trailer       = 0x34383238
 * ```
 */
object SessionSecretFrame {
    /** Frame payload schema version (native kSessionSecretFrameVersion). */
    const val VERSION: UByte = 1u

    /** kind == 1: IpSec SA parameters (native kSessionSecretFrameKindIpsecSa). */
    const val KIND_IPSEC_SA: UByte = 1u

    /** Truncation/misalignment sentinel (native kSessionSecretFrameTrailer). */
    const val TRAILER: UInt = 0x34383238u

    /** Fixed payload size, excluding the 4-byte length prefix. */
    const val PAYLOAD_SIZE: Int = 84

    /** 4-byte big-endian length prefix. */
    const val LENGTH_PREFIX_SIZE: Int = 4

    /** Maximum accepted declared length, mirroring the native cap. */
    const val MAX_PAYLOAD_SIZE: Int = 4096

    /** HMAC-SHA256 truncated to 128 bits (native kEspIcvBytes). */
    const val ICV_LEN: UByte = 16u

    /** AES/HMAC key width. */
    const val KEY_BYTES: Int = 32

    private const val RESERVED2_BYTES = 3

    /** Encodes the 84-byte payload without the length prefix. */
    fun encode(secrets: IpsecSessionSecrets): ByteArray {
        val buffer = ByteBuffer.allocate(PAYLOAD_SIZE).order(ByteOrder.BIG_ENDIAN)
        buffer.put(VERSION.toByte())
        buffer.put(KIND_IPSEC_SA.toByte())
        buffer.putShort(0)
        buffer.putInt(secrets.spi.toInt())
        buffer.putShort(secrets.encapPort.toShort())
        buffer.putShort(secrets.senderPort.toShort())
        buffer.put(secrets.icvLen.toByte())
        buffer.put(ByteArray(RESERVED2_BYTES))
        buffer.put(secrets.aesKey)
        buffer.put(secrets.hmacKey)
        buffer.putInt(TRAILER.toInt())
        return buffer.array()
    }

    /** Encodes the payload with its 4-byte big-endian length prefix. */
    fun encodeFramed(secrets: IpsecSessionSecrets): ByteArray =
        lengthPrefix(PAYLOAD_SIZE) + encode(secrets)

    /**
     * Builds the complete cve_2026_43284 app-call stdin stream: the
     * length-prefixed GLKv3 document followed by the length-prefixed session
     * frame. The GLKv3 byte sequence is left untouched, so existing goldens
     * stay valid.
     */
    fun appCallStdin(glkv3Document: ByteArray, secrets: IpsecSessionSecrets): ByteArray =
        lengthPrefix(glkv3Document.size) + glkv3Document + encodeFramed(secrets)

    /** 4-byte big-endian length prefix. */
    fun lengthPrefix(size: Int): ByteArray {
        require(size >= 0) { "frame length must be non-negative" }
        return byteArrayOf(
            (size ushr 24).toByte(),
            (size ushr 16).toByte(),
            (size ushr 8).toByte(),
            size.toByte(),
        )
    }
}
