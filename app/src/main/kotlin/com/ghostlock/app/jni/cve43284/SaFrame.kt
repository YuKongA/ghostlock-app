package com.ghostlock.app.jni.cve43284

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.nio.file.Paths

/**
 *  会话秘密帧（84 字节 payload）的装配器 —— 纯 Kotlin，无 native 依赖。
 *
 *  格式冻结于 backend/cve_2026_43284/session_frame.hpp，任何不匹配都 fail-closed，所以这里逐字段照抄：
 *
 *      u32 be payload_len = 84（4 字节前缀，不含在 84 里）
 *      +0  u8  version = 1
 *      +1  u8  kind = 1 (IPSEC_SA)
 *      +2  u16 be reserved = 0
 *      +4  u32 be spi
 *      +8  u16 be encap_port
 *      +10 u16 be sender_port
 *      +12 u8  icv_len = 16
 *      +13 u8  reserved2[3] = 0
 *      +16 u8  aes_key[32]
 *      +48 u8  hmac_key[32]
 *      +80 u32 be trailer = 0x34383238
 *
 *  整数一律大端；两个密钥各 32 字节。
 */
object SaFrame {

    const val PAYLOAD_BYTES = 84
    const val VERSION = 1
    const val KIND_IPSEC_SA = 1
    const val ICV_BYTES = 16
    const val TRAILER = 0x34383238
    const val KEY_BYTES = 32

    /** @return 帧字节（4 字节长度前缀 + 84 字节 payload）。 */
    fun build(
        spi: Int,
        encapPort: Int,
        senderPort: Int,
        aesKey: ByteArray,
        hmacKey: ByteArray,
    ): ByteArray {
        require(aesKey.size == KEY_BYTES) { "aes_key must be 32 bytes" }
        require(hmacKey.size == KEY_BYTES) { "hmac_key must be 32 bytes" }
        require(encapPort in 1..65535) { "encap_port out of range" }
        require(senderPort in 0..65535) { "sender_port out of range" }

        val payload = ByteBuffer.allocate(PAYLOAD_BYTES).order(ByteOrder.BIG_ENDIAN)
        payload.put(VERSION.toByte())
        payload.put(KIND_IPSEC_SA.toByte())
        payload.putShort(0)
        payload.putInt(spi)
        payload.putShort(encapPort.toShort())
        payload.putShort(senderPort.toShort())
        payload.put(ICV_BYTES.toByte())
        payload.put(byteArrayOf(0, 0, 0))
        payload.put(aesKey)
        payload.put(hmacKey)
        payload.putInt(TRAILER)

        val out = ByteArrayOutputStream(4 + PAYLOAD_BYTES)
        out.write(ByteBuffer.allocate(4).order(ByteOrder.BIG_ENDIAN).putInt(PAYLOAD_BYTES).array())
        out.write(payload.array())
        return out.toByteArray()
    }

    /** 落盘到 Native 侧约定的帧路径（read_session_secret_frame 读它）。@return 写入字节数。 */
    fun writeTo(path: String, frame: ByteArray): Int {
        Files.write(Paths.get(path), frame)
        return frame.size
    }
}
