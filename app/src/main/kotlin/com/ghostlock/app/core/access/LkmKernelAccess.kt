package com.ghostlock.app.core.access

import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.CleanupDisposition
import com.ghostlock.app.core.SessionPhase
import java.io.FileDescriptor
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * /dev/glk 请求（与 tools/lkm/ghostlock/ghostlock.glk.c 的 struct glk_lkm_req 对齐）。
 *
 * 布局（小端，32 字节）：
 *   u32 abi_version | u32 op | u64 addr | u64 value | u32 len | u32 status
 *
 * value 是**用户态缓冲区地址**：READ 时内核 copy_to_user 到该地址，
 * WRITE 时从该地址 copy_from_user。
 */
data class GlkRequest(
    val abiVersion: Int = GLK_ABI_VERSION,
    val op: Int,
    val address: Long = 0L,
    val value: Long = 0L,
    val length: Int = 0,
    val status: Int = 0,
) {
    fun encode(): ByteBuffer {
        val buffer = ByteBuffer.allocateDirect(SIZE).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(abiVersion)
        buffer.putInt(op)
        buffer.putLong(address)
        buffer.putLong(value)
        buffer.putInt(length)
        buffer.putInt(status)
        buffer.flip()
        return buffer
    }

    companion object {
        const val SIZE = 32
        const val GLK_ABI_VERSION = 1
        const val IOCTL = 0x6747
        const val MAX_TRANSFER = 4096

        const val OP_PING = 0
        const val OP_READ = 1
        const val OP_WRITE = 2
        const val OP_WRITE_ZERO = 3
        const val OP_UNLOAD = 7

        fun decode(buffer: ByteBuffer): GlkRequest {
            val copy = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
            copy.position(0)
            return GlkRequest(copy.int, copy.int, copy.long, copy.long, copy.int, copy.int)
        }
    }
}

/** ioctl 传输缝：真机走 libcore，测试注入假实现。 */
fun interface GlkTransport {
    fun ioctl(fd: FileDescriptor, request: ByteBuffer, data: ByteBuffer?): Int
}

/**
 * 默认传输：反射 libcore 的 ioctl。
 *
 * Android 没有公开“带指针参数的 ioctl”，所以走隐藏接口
 * libcore.io.Libcore.os.ioctl(FileDescriptor, int, Object)；不可用即返回 -1。
 */
object ReflectiveGlkTransport : GlkTransport {
    override fun ioctl(fd: FileDescriptor, request: ByteBuffer, data: ByteBuffer?): Int = runCatching {
        val libcore = Class.forName("libcore.io.Libcore").getField("os").get(null)
        val method = libcore.javaClass.methods.firstOrNull {
            it.name == "ioctl" && it.parameterTypes.size == 3 &&
                it.parameterTypes[0] == java.io.FileDescriptor::class.java
        } ?: return -1
        method.invoke(libcore, fd, GlkRequest.IOCTL, request)
        request.position(24)
        val status = request.int
        if (status == 0) 0 else -status
    }.getOrDefault(-1)
}

/**
 * LKM 会话：Kotlin 编排、fd 由本类持有并在 shutdown 关闭；能力视图借用同一 fd。
 *
 * 调用串行化用 Mutex：同一 handle 上的 native 操作不并发，避免一次事务被另一次调用切断。
 */
class LkmGlkSession(
    private val path: String = "/dev/glk",
    private val transport: GlkTransport = ReflectiveGlkTransport,
    private val open: (String) -> FileDescriptor = { Os.open(it, OsConstants.O_RDWR or OsConstants.O_CLOEXEC, 0) },
    private val close: (FileDescriptor) -> Unit = { Os.close(it) },
) : AccessSession, AccessCapabilities {
    private val gate = Mutex()
    private var fd: FileDescriptor? = null
    private var generation: UInt = 0u
    private var ready = false
    private var failed = false
    private var closed = false

    private val kernelView = object : KernelMemoryAccess {
        override val descriptor: KernelMemoryDescriptor
            get() = descriptorNow()

        override suspend fun read(address: KernelAddress, bytes: Int): AccessResult<ByteArray> =
            gate.withLock { readLocked(address.value, bytes) }

        override suspend fun write(address: KernelAddress, bytes: ByteArray): AccessResult<WriteReceipt> =
            gate.withLock { writeLocked(address.value, bytes) }
    }

    override val kernelMemory: KernelMemoryAccess get() = kernelView
    override val pageCache: PageCacheAccess? get() = null

    override val capabilities: AccessCapabilities get() = this

    override val phase: SessionPhase get() = when {
        closed -> SessionPhase.CLOSED
        failed -> SessionPhase.FAILED
        ready -> SessionPhase.READY
        else -> SessionPhase.INITIALIZING
    }

    override val cleanup: CleanupDisposition get() = CleanupDisposition.CLEAN

    /** Cancel-only: fd/unload cleanup remains in shutdown(). */
    override fun close() {
        closed = true
        ready = false
    }

    private fun descriptorNow(): KernelMemoryDescriptor {
        val handleReady = ready && fd != null
        val constraint = if (handleReady) TransferConstraint(granularity = 1, maxBytes = GlkRequest.MAX_TRANSFER) else null
        return KernelMemoryDescriptor(
            read = constraint,
            write = constraint,
            addressRanges = if (handleReady) listOf(KernelAddressRange(Long.MIN_VALUE, Long.MAX_VALUE)) else emptyList(),
            generation = generation,
        )
    }

    override suspend fun snapshot(): AccessResult<SessionSnapshot> = gate.withLock {
        val phase = when {
            failed -> "failed"
            ready -> "ready"
            fd != null -> "initializing"
            else -> "declared"
        }
        AccessResult.Success(
            SessionSnapshot(phase = phase, capabilities = mapOf("kernelMemory" to descriptorNow())),
        )
    }

    override suspend fun initialize(): AccessResult<InitializationReceipt> = gate.withLock {
        if (closed) return@withLock AccessResult.Failure(AccessFailure.NOT_READY, "session closed")
        if (fd != null) {
            return@withLock AccessResult.Success(InitializationReceipt(listOf(descriptorNow())))
        }
        val opened = runCatching { open(path) }.getOrElse { error ->
            failed = true
            return@withLock AccessResult.Failure(AccessFailure.NOT_READY, error.message ?: "open /dev/glk failed")
        }
        fd = opened
        val ping = callLocked(GlkRequest(op = GlkRequest.OP_PING))
        if (ping is AccessResult.Failure) {
            fd = null
            runCatching { close(opened) }
            failed = true
            return@withLock ping
        }
        ready = true
        failed = false
        generation += 1u
        AccessResult.Success(InitializationReceipt(listOf(descriptorNow())))
    }

    override suspend fun shutdown(): AccessResult<ShutdownReceipt> = gate.withLock {
        closed = true
        val handle = fd
        if (handle == null) return@withLock AccessResult.Success(ShutdownReceipt(clean = true))
        val unload = callLocked(GlkRequest(op = GlkRequest.OP_UNLOAD))
        fd = null
        ready = false
        generation += 1u
        runCatching { close(handle) }
        when (unload) {
            is AccessResult.Failure -> AccessResult.Success(ShutdownReceipt(clean = false, detail = unload.detail))
            is AccessResult.Success -> AccessResult.Success(ShutdownReceipt(clean = true))
        }
    }

    private fun readLocked(address: Long, bytes: Int): AccessResult<ByteArray> {
        if (!ready || fd == null) return AccessResult.Failure(AccessFailure.NOT_READY, "session not ready")
        if (bytes !in 1..GlkRequest.MAX_TRANSFER) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "bad width")
        }
        val data = ByteBuffer.allocateDirect(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val request = GlkRequest(
            op = GlkRequest.OP_READ,
            address = address,
            length = bytes,
            value = addressOf(data),
        )
        val result = callLocked(request, data)
        if (result is AccessResult.Failure) return result
        val out = ByteArray(bytes)
        data.position(0)
        data.get(out)
        return AccessResult.Success(out)
    }

    private fun writeLocked(address: Long, bytes: ByteArray): AccessResult<WriteReceipt> {
        if (!ready || fd == null) return AccessResult.Failure(AccessFailure.NOT_READY, "session not ready")
        if (bytes.isEmpty() || bytes.size > GlkRequest.MAX_TRANSFER) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "bad width")
        }
        val buffer = ByteBuffer.allocateDirect(bytes.size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(bytes)
        buffer.flip()
        val request = GlkRequest(
            op = GlkRequest.OP_WRITE,
            address = address,
            length = bytes.size,
            value = addressOf(buffer),
        )
        val result = callLocked(request, buffer)
        return when (result) {
            is AccessResult.Failure -> AccessResult.Success(
                WriteReceipt(bytes = bytes.size, effect = EffectState.UNKNOWN, generation = generation),
            )
            is AccessResult.Success -> AccessResult.Success(
                WriteReceipt(bytes = bytes.size, effect = EffectState.CONFIRMED, generation = generation),
            )
        }
    }

    private fun callLocked(request: GlkRequest, data: ByteBuffer? = null): AccessResult<Unit> {
        val handle = fd ?: return AccessResult.Failure(AccessFailure.NOT_READY, "no handle")
        val encoded = request.encode()
        val rc = transport.ioctl(handle, encoded, data)
        if (rc < 0) {
            failed = true
            return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "ioctl failed")
        }
        val decoded = GlkRequest.decode(encoded)
        if (decoded.status != 0) {
            failed = true
            return AccessResult.Failure(AccessFailure.NATIVE_FAILURE, "status=" + decoded.status)
        }
        return AccessResult.Success(Unit)
    }

    private fun addressOf(buffer: ByteBuffer): Long = runCatching {
        val field = java.nio.Buffer::class.java.getDeclaredField("address")
        field.isAccessible = true
        field.getLong(buffer)
    }.getOrDefault(0L)
}
