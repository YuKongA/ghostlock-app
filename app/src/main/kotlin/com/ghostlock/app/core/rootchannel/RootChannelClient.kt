package com.ghostlock.app.core.rootchannel

import android.content.Context
import android.net.LocalSocket
import android.net.LocalSocketAddress
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File

/**
 * 内置 root 通道客户端：优先走攻击链拉起的 root helper，而不是 su。
 *
 * 服务端（root helper）由攻击链用 call_usermodehelper / root_child 启动，
 * 在 [socketPath] 监听；客户端只负责发命令、收输出、收退出码。
 * 权限校验（只允许本应用 uid）由服务端负责。
 */
object RootChannelClient {
    private const val CONNECT_TIMEOUT_MS = 300

    /** 通道 socket 的固定路径（root helper 必须监听同一路径）。 */
    fun socketPath(context: Context): String = File(context.filesDir, "root-channel.sock").absolutePath

    /** 通道是否看起来可用（socket 文件存在）。 */
    fun available(context: Context): Boolean = File(socketPath(context)).exists()

    /**
     * 把 rootd 从 assets 复制到外部目录，供 root 侧复制到 /data/local/tmp 执行。
     *
     * @return 外部路径；asset 缺失时返回 null。
     */
    fun stageServer(context: Context): String? {
        val dir = context.getExternalFilesDir(null)?.resolve("finish") ?: return null
        val dest = File(dir, "ghostlock-rootd")
        return runCatching {
            dir.mkdirs()
            context.assets.open("rootd/ghostlock-rootd").use { input ->
                dest.outputStream().use { output -> input.copyTo(output) }
            }
            dest.setExecutable(true, false)
            dest.absolutePath
        }.getOrNull()
    }

    /**
     * 通过 root 通道执行一条 shell 命令。
     *
     * @param onLine 逐行回调（stdout/stderr 合并，保留原始行）。
     * @return 退出码；通道不可用/连接失败返回 null（调用方回退 su）。
     */
    fun exec(
        context: Context,
        command: String,
        stdin: ByteArray = ByteArray(0),
        onLine: (String) -> Unit,
    ): Int? {
        val path = socketPath(context)
        if (!File(path).exists()) return null
        val socket = LocalSocket()
        return try {
            socket.connect(LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM))
            val output = DataOutputStream(socket.outputStream)
            val input = DataInputStream(socket.inputStream)
            val request = RootChannelProtocol.Request(command, stdin).encode()
            output.write(request.array(), request.arrayOffset() + request.position(), request.remaining())
            output.flush()

            var exit: Int? = null
            val partial = StringBuilder()
            while (exit == null) {
                val type = input.readUnsignedByte()
                val lengthBytes = ByteArray(4)
                input.readFully(lengthBytes)
                val length = java.nio.ByteBuffer.wrap(lengthBytes)
                    .order(java.nio.ByteOrder.LITTLE_ENDIAN).int
                if (length < 0 || length > RootChannelProtocol.MAX_FRAME) return null
                val payload = ByteArray(length)
                input.readFully(payload)
                when (type) {
                    RootChannelProtocol.TYPE_EXIT -> {
                        exit = RootChannelProtocol.Frame(type, payload).exitCode
                    }
                    RootChannelProtocol.TYPE_STDOUT, RootChannelProtocol.TYPE_STDERR -> {
                        partial.append(String(payload, Charsets.UTF_8))
                        while (true) {
                            val newline = partial.indexOf("\n")
                            if (newline < 0) break
                            onLine(partial.substring(0, newline))
                            partial.delete(0, newline + 1)
                        }
                    }
                }
            }
            if (partial.isNotEmpty()) onLine(partial.toString())
            exit
        } catch (_: Throwable) {
            null
        } finally {
            runCatching { socket.close() }
        }
    }
}
