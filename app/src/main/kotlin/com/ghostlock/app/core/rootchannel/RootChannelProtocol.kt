package com.ghostlock.app.core.rootchannel

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * RootChannel 的线协议（客户端侧权威；root helper 必须按同一布局实现）。
 *
 * 请求（小端）：
 *   magic u32 = 0x474C5231（"GLR1"）
 *   commandLen u32 + command bytes（UTF-8，交给 root 侧 `sh -c`）
 *   stdinLen u32 + stdin bytes（可为 0）
 *
 * 响应帧：
 *   type u8：1=stdout，2=stderr，3=exit
 *   len u32
 *   payload（exit 帧的 payload 是 u32 退出码）
 */
object RootChannelProtocol {
    const val MAGIC: Int = 0x474C5231
    const val TYPE_STDOUT: Int = 1
    const val TYPE_STDERR: Int = 2
    const val TYPE_EXIT: Int = 3
    const val MAX_FRAME: Int = 64 * 1024

    data class Request(val command: String, val stdin: ByteArray = ByteArray(0)) {
        fun encode(): ByteBuffer {
            val cmd = command.toByteArray(Charsets.UTF_8)
            val buffer = ByteBuffer.allocate(4 + 4 + cmd.size + 4 + stdin.size).order(ByteOrder.LITTLE_ENDIAN)
            buffer.putInt(MAGIC)
            buffer.putInt(cmd.size)
            buffer.put(cmd)
            buffer.putInt(stdin.size)
            buffer.put(stdin)
            buffer.flip()
            return buffer
        }
    }

    data class Frame(val type: Int, val payload: ByteArray) {
        val exitCode: Int?
            get() = if (type == TYPE_EXIT && payload.size >= 4) {
                ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN).int
            } else {
                null
            }
    }

    fun decodeRequest(buffer: ByteBuffer): Request? {
        val copy = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        copy.position(0)
        if (copy.remaining() < 8 || copy.int != MAGIC) return null
        val commandLen = copy.int
        if (commandLen < 0 || commandLen > copy.remaining()) return null
        val command = ByteArray(commandLen).also { copy.get(it) }
        if (copy.remaining() < 4) return null
        val stdinLen = copy.int
        if (stdinLen < 0 || stdinLen > copy.remaining()) return null
        val stdin = ByteArray(stdinLen).also { copy.get(it) }
        return Request(String(command, Charsets.UTF_8), stdin)
    }

    fun encodeFrame(type: Int, payload: ByteArray): ByteBuffer {
        val buffer = ByteBuffer.allocate(1 + 4 + payload.size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(type.toByte())
        buffer.putInt(payload.size)
        buffer.put(payload)
        buffer.flip()
        return buffer
    }

    fun encodeExit(code: Int): ByteBuffer =
        encodeFrame(TYPE_EXIT, ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(code).array())
}
