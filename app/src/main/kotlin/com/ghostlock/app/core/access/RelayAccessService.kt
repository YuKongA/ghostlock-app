package com.ghostlock.app.core.access

import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.net.InetAddress
import java.net.ServerSocket
import java.net.SocketTimeoutException
import java.nio.ByteOrder
import kotlinx.coroutines.runBlocking

/**
 * 43284 relay 的内核内存访问窗口。
 *
 * 脚本 insmod 中继 LKM 后内核创建 [DEVICE]；本服务看到设备节点后打开它，
 * 在 [PORT] 上等待插件连接最多 [ACCEPT_TIMEOUT_MS] 毫秒：
 *  - 有连接：按帧协议服务，插件关闭后结束窗口；
 *  - 超时/无连接：直接结束窗口，脚本继续收尾。
 *
 * 不使用任何 marker 文件；设备节点的出现与消失就是窗口边界。
 *
 * 帧协议（大端）：
 *   请求 = op(1) | addr(8) | len(4) | [payload(len)]
 *     op: 1 = read, 2 = write
 *   响应 = status(1) | len(4) | [payload(len)]
 *     status: 0 = ok, 1 = failed
 */
object RelayAccessService {
    const val DEVICE = "/dev/glk"
    const val PORT = 39284
    const val ACCEPT_TIMEOUT_MS = 5000
    private const val READY_WAIT_MS = 10_000L

    fun serve(emit: (String) -> Unit = {}) {
        val device = File(DEVICE)
        val deadline = System.currentTimeMillis() + READY_WAIT_MS
        while (!device.exists() && System.currentTimeMillis() < deadline) {
            Thread.sleep(50)
        }
        if (!device.exists()) {
            emit("relay access: device missing; skip")
            return
        }
        runBlocking {
            val session = LkmGlkSession()
            when (val init = session.initialize()) {
                is com.ghostlock.app.core.AccessResult.Failure -> {
                    emit("relay access: initialize failed: " + init.detail)
                    return@runBlocking
                }
                is com.ghostlock.app.core.AccessResult.Success -> emit("relay access: /dev/glk ready")
            }
            ServerSocket(PORT, 1, InetAddress.getLoopbackAddress()).use { server ->
                server.soTimeout = ACCEPT_TIMEOUT_MS
                val client = try {
                    server.accept()
                } catch (_: SocketTimeoutException) {
                    emit("relay access: no connector within 5s")
                    null
                }
                if (client == null) {
                    session.shutdown()
                    return@runBlocking
                }
                emit("relay access: connector accepted")
                try {
                    serveClient(client, session, emit)
                } finally {
                    emit("relay access: connector closed")
                    session.shutdown()
                }
            }
        }
    }

    private fun serveClient(client: java.net.Socket, session: LkmGlkSession, emit: (String) -> Unit) {
        val input = DataInputStream(client.getInputStream().buffered())
        val output = DataOutputStream(client.getOutputStream().buffered())
        while (true) {
            val op = try { input.readUnsignedByte() } catch (_: Exception) { break }
            val address = try { input.readLong() } catch (_: Exception) { break }
            val length = try { input.readInt() } catch (_: Exception) { break }
            if (length < 0 || length > GlkRequest.MAX_TRANSFER) break
            val data = ByteArray(length)
            if (op == OP_WRITE) {
                try { input.readFully(data) } catch (_: Exception) { break }
            }
            when (op) {
                OP_READ -> {
                    val result = runBlocking { session.kernelMemory.read(KernelAddress(address), length) }
                    when (result) {
                        is com.ghostlock.app.core.AccessResult.Success -> writeResponse(output, result.value)
                        is com.ghostlock.app.core.AccessResult.Failure -> {
                            writeFailure(output)
                            emit("relay access: read failed: " + result.detail)
                        }
                    }
                }
                OP_WRITE -> {
                    val result = runBlocking { session.kernelMemory.write(KernelAddress(address), data) }
                    when (result) {
                        is com.ghostlock.app.core.AccessResult.Success -> writeResponse(output, ByteArray(0))
                        is com.ghostlock.app.core.AccessResult.Failure -> {
                            writeFailure(output)
                            emit("relay access: write failed: " + result.detail)
                        }
                    }
                }
                else -> break
            }
        }
    }

    private fun writeResponse(output: DataOutputStream, payload: ByteArray) {
        output.writeByte(0)
        output.writeInt(payload.size)
        if (payload.isNotEmpty()) output.write(payload)
        output.flush()
    }

    private fun writeFailure(output: DataOutputStream) {
        output.writeByte(1)
        output.writeInt(0)
        output.flush()
    }

    private const val OP_READ = 1
    private const val OP_WRITE = 2
}
