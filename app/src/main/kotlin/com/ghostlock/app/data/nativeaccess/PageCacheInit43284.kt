package com.ghostlock.app.data.nativeaccess

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * 43284 能力会话的初始化材料。
 *
 * 与 native 的 `AccessParams` 对齐：SA 的 spi/端口/ICV/两把密钥，加上载体路径和 sequence。
 * 密钥只在这里的字节数组里出现，不写日志。
 */
data class PageCacheInit43284(
    val spi: Int,
    val encapPort: Int,
    val senderPort: Int,
    val icvLen: Int,
    val aesKey: ByteArray,
    val hmacKey: ByteArray,
    val carrierPath: String,
    val sequence: Long = 0L,
) {
    init {
        require(aesKey.size == 32) { "AES key must be 32 bytes" }
        require(hmacKey.size == 32) { "HMAC key must be 32 bytes" }
        require(carrierPath.isNotEmpty()) { "carrier path is required" }
    }

    /** 73 字节小端：spi(4) encap(2) sender(2) icv(1) aes(32) hmac(32)。 */
    fun secret(): ByteArray = ByteBuffer.allocate(73).order(ByteOrder.LITTLE_ENDIAN).apply {
        putInt(spi)
        putShort(encapPort.toShort())
        putShort(senderPort.toShort())
        put(icvLen.toByte())
        put(aesKey)
        put(hmacKey)
    }.array()
}
