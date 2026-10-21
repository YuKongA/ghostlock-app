package com.ghostlock.app.engine.ktstep

/**
 * 唯一 JNI syscall 面。每个 external 对应 C++ `native_syscalls_43499.cpp` 的一条 syscall；
 * 返回 `>=0 成功 / <0 -errno`。
 */
object NativeSyscalls43499 {
    const val LIBRARY = "gl43499syscalls"

    @Volatile
    private var loaded = false

    /** @return 库可用；失败时调用方得到 ENOSYS，不抛到调用点。 */
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

    external fun futex(uaddr: Long, op: Int, value: Int, timeoutNs: Long, uaddr2: Long, value3: Int): Long
    external fun mmap(length: Long, prot: Int, flags: Int, fd: Int, offset: Long): Long
    external fun munmap(address: Long, length: Long): Long
    external fun memfdCreate(name: String, flags: Int): Long
    external fun socket(domain: Int, type: Int, protocol: Int): Long
    external fun setsockopt(fd: Int, level: Int, option: Int, value: ByteArray): Long
    external fun send(fd: Int, buffer: ByteArray, flags: Int): Long
    external fun recv(fd: Int, buffer: ByteArray, flags: Int): Long
    external fun close(fd: Int): Long
    external fun read(fd: Int, buffer: ByteArray): Long
    external fun write(fd: Int, buffer: ByteArray): Long
    external fun perfEventOpen(attr: ByteArray, pid: Int, cpu: Int, groupFd: Int, flags: Long): Long
    external fun ioctl(fd: Int, request: Long, arg: Long): Long
    external fun gettid(): Long
    external fun getpid(): Long
    external fun processVmWritev(pid: Int, address: Long, buffer: ByteArray): Long
    external fun processVmReadv(pid: Int, address: Long, buffer: ByteArray): Long
    external fun usleep(micros: Long): Long
    external fun clockGettimeNs(clockId: Int): Long
    external fun pipe2(out: IntArray, flags: Int): Long
    external fun schedSetaffinity(pid: Int, cpuMask: Long): Long
    external fun schedSetattr(tid: Int, attr: ByteArray): Long
    external fun schedGetcpu(): Long
    external fun rseq(rseq: ByteArray, flags: Int, signature: Int): Long
}

/** 生产实现：库缺失时每个调用 fail-closed 为 ENOSYS。 */
class NativeKernelSyscalls : KernelSyscalls {
    private inline fun call(block: () -> Long): SyscallResult =
        if (NativeSyscalls43499.ensureLoaded()) {
            SyscallResult.of(block())
        } else {
            SyscallResult.Errno(KernelErrno.ENOSYS)
        }

    override fun futex(uaddr: Long, op: Int, value: Int, timeoutNs: Long, uaddr2: Long, value3: Int) =
        call { NativeSyscalls43499.futex(uaddr, op, value, timeoutNs, uaddr2, value3) }
    override fun mmap(length: Long, prot: Int, flags: Int, fd: Int, offset: Long) =
        call { NativeSyscalls43499.mmap(length, prot, flags, fd, offset) }
    override fun munmap(address: Long, length: Long) =
        call { NativeSyscalls43499.munmap(address, length) }
    override fun memfdCreate(name: String, flags: Int) =
        call { NativeSyscalls43499.memfdCreate(name, flags) }
    override fun socket(domain: Int, type: Int, protocol: Int) =
        call { NativeSyscalls43499.socket(domain, type, protocol) }
    override fun setsockopt(fd: Int, level: Int, option: Int, value: ByteArray) =
        call { NativeSyscalls43499.setsockopt(fd, level, option, value) }
    override fun send(fd: Int, buffer: ByteArray, flags: Int) =
        call { NativeSyscalls43499.send(fd, buffer, flags) }
    override fun recv(fd: Int, buffer: ByteArray, flags: Int) =
        call { NativeSyscalls43499.recv(fd, buffer, flags) }
    override fun close(fd: Int) = call { NativeSyscalls43499.close(fd) }
    override fun read(fd: Int, buffer: ByteArray) = call { NativeSyscalls43499.read(fd, buffer) }
    override fun write(fd: Int, buffer: ByteArray) = call { NativeSyscalls43499.write(fd, buffer) }
    override fun perfEventOpen(attr: ByteArray, pid: Int, cpu: Int, groupFd: Int, flags: Long) =
        call { NativeSyscalls43499.perfEventOpen(attr, pid, cpu, groupFd, flags) }
    override fun ioctl(fd: Int, request: Long, arg: Long) =
        call { NativeSyscalls43499.ioctl(fd, request, arg) }
    override fun gettid() = call { NativeSyscalls43499.gettid() }
    override fun getpid() = call { NativeSyscalls43499.getpid() }
    override fun processVmWritev(pid: Int, address: Long, buffer: ByteArray) =
        call { NativeSyscalls43499.processVmWritev(pid, address, buffer) }
    override fun processVmReadv(pid: Int, address: Long, buffer: ByteArray) =
        call { NativeSyscalls43499.processVmReadv(pid, address, buffer) }
    override fun usleep(micros: Long) = call { NativeSyscalls43499.usleep(micros) }
    override fun clockGettimeNs(clockId: Int) = call { NativeSyscalls43499.clockGettimeNs(clockId) }
    override fun pipe2(out: IntArray, flags: Int) = call { NativeSyscalls43499.pipe2(out, flags) }
    override fun schedSetaffinity(pid: Int, cpuMask: Long) =
        call { NativeSyscalls43499.schedSetaffinity(pid, cpuMask) }
    override fun schedSetattr(tid: Int, attr: ByteArray) =
        call { NativeSyscalls43499.schedSetattr(tid, attr) }
    override fun schedGetcpu() = call { NativeSyscalls43499.schedGetcpu() }
    override fun rseq(rseq: ByteArray, flags: Int, signature: Int) =
        call { NativeSyscalls43499.rseq(rseq, flags, signature) }
}
