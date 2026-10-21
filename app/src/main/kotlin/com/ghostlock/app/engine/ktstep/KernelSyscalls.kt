package com.ghostlock.app.engine.ktstep

/**
 * 43499 kt-step 的 syscall 结果：成功带值，失败只带 errno（正数）。
 *
 * 原生包装统一用“负值 = -errno”返回；这里只做一次翻译，不吞错误。
 */
sealed interface SyscallResult {
    data class Ok(val value: Long) : SyscallResult
    data class Errno(val code: Int) : SyscallResult

    companion object {
        /** 把原生 `>=0 成功 / <0 -errno` 翻成结果。 */
        fun of(raw: Long): SyscallResult =
            if (raw < 0L) Errno((-raw).toInt()) else Ok(raw)
    }
}

/** Linux/Android 通用 errno 值；只列 43499 路径会用到的。 */
object KernelErrno {
    const val EPERM = 1
    const val ENOENT = 2
    const val ESRCH = 3
    const val EINTR = 4
    const val EBADF = 9
    const val EAGAIN = 11
    const val ENOMEM = 12
    const val EACCES = 13
    const val EFAULT = 14
    const val EBUSY = 16
    const val EINVAL = 22
    const val ENFILE = 23
    const val EMFILE = 24
    const val ENOSPC = 28
    const val EPIPE = 32
    const val EDEADLK = 35
    const val ENOSYS = 38
    const val EOVERFLOW = 75
    const val ENOTSUP = 95
    const val ETIMEDOUT = 110
}

/**
 * 43499 的薄 syscall 面。所有方法只表达一次系统调用，不做重试/路由/页/会话决策。
 *
 * 地址/长度用 Long（64 位设备）；ByteArray 由调用方持有，JNI 只在调用期间借用。
 */
interface KernelSyscalls {
    fun futex(uaddr: Long, op: Int, value: Int, timeoutNs: Long, uaddr2: Long, value3: Int): SyscallResult
    fun mmap(length: Long, prot: Int, flags: Int, fd: Int, offset: Long): SyscallResult
    fun munmap(address: Long, length: Long): SyscallResult
    fun memfdCreate(name: String, flags: Int): SyscallResult
    fun socket(domain: Int, type: Int, protocol: Int): SyscallResult
    fun setsockopt(fd: Int, level: Int, option: Int, value: ByteArray): SyscallResult
    fun send(fd: Int, buffer: ByteArray, flags: Int): SyscallResult
    fun recv(fd: Int, buffer: ByteArray, flags: Int): SyscallResult
    fun close(fd: Int): SyscallResult
    fun read(fd: Int, buffer: ByteArray): SyscallResult
    fun write(fd: Int, buffer: ByteArray): SyscallResult
    fun perfEventOpen(attr: ByteArray, pid: Int, cpu: Int, groupFd: Int, flags: Long): SyscallResult
    fun ioctl(fd: Int, request: Long, arg: Long): SyscallResult
    fun gettid(): SyscallResult
    fun getpid(): SyscallResult
    fun processVmWritev(pid: Int, address: Long, buffer: ByteArray): SyscallResult
    fun processVmReadv(pid: Int, address: Long, buffer: ByteArray): SyscallResult
    fun usleep(micros: Long): SyscallResult
    fun clockGettimeNs(clockId: Int): SyscallResult
    fun pipe2(out: IntArray, flags: Int): SyscallResult
    fun schedSetaffinity(pid: Int, cpuMask: Long): SyscallResult
    fun schedSetattr(tid: Int, attr: ByteArray): SyscallResult
    fun schedGetcpu(): SyscallResult
    fun rseq(rseq: ByteArray, flags: Int, signature: Int): SyscallResult
}
