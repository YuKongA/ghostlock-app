package com.ghostlock.app.engine.ktstep

/** N2 的唯一 native 库入口；库缺失时调用方 fail-closed。 */
object NativeAttemptJni {
    const val LIBRARY = "gl43499native"

    @Volatile
    private var loaded = false

    @Synchronized
    fun ensureLoaded(): Boolean {
        if (loaded) return true
        loaded = try {
            System.loadLibrary(LIBRARY)
            true
        } catch (missing: UnsatisfiedLinkError) {
            false
        }
        return loaded
    }

    external fun create(flags: Int): Long
    external fun attempt(handle: Long, params: ByteArray): ByteArray?
    external fun spawnChild(handle: Long, params: ByteArray): Long
    external fun childCommand(handle: Long, cmd: Int, payload: ByteArray): Long
    external fun childFinish(handle: Long, action: Int): Long
    external fun close(handle: Long): Long
}

/** Kotlin port 实现：只做 blob 编解码，不解释 route/成功。 */
class NativeJniAttemptPort : NativeAttemptPort {
    @Volatile
    private var handle: Long = 0L

    @Synchronized
    override fun open(): SyscallResult {
        if (!NativeAttemptJni.ensureLoaded()) return SyscallResult.Errno(KernelErrno.ENOSYS)
        if (handle != 0L) return SyscallResult.Ok(handle)
        val created = NativeAttemptJni.create(0)
        if (created <= 0L) return SyscallResult.Errno(KernelErrno.ENOSYS)
        handle = created
        return SyscallResult.Ok(created)
    }

    override fun attempt(params: NativeAttemptParams): NativeAttemptResult {
        val current = handle
        if (current == 0L) return NativeAttemptResult.Errno(KernelErrno.ENOSYS)
        val encoded = when (val packed = NativeAttemptCodec.encode(params)) {
            is NativeAttemptCodec.Encoded.Ok -> packed.bytes
            is NativeAttemptCodec.Encoded.Invalid -> return NativeAttemptResult.Malformed(packed.reason)
        }
        val answer = NativeAttemptJni.attempt(current, encoded)
            ?: return NativeAttemptResult.Malformed("null")
        return NativeAttemptCodec.decodeResult(answer)
    }

    override fun spawnChild(params: NativeChildParams) = SyscallResult.Errno(KernelErrno.ENOSYS)

    override fun childCommand(cmd: Int, payload: ByteArray) = SyscallResult.Errno(KernelErrno.ENOSYS)

    override fun childFinish(action: Int) = SyscallResult.Errno(KernelErrno.ENOSYS)

    @Synchronized
    override fun close(): SyscallResult {
        val current = handle
        handle = 0L
        if (current == 0L) return SyscallResult.Ok(0L)
        return SyscallResult.of(NativeAttemptJni.close(current))
    }
}
