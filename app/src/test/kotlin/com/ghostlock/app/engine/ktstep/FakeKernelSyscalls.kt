package com.ghostlock.app.engine.ktstep

/** 记录调用并按方法名脚本化结果；host 测试不加载 .so。 */
class FakeKernelSyscalls : KernelSyscalls {
    val calls = mutableListOf<String>()
    val futexOps = mutableListOf<Int>()
    val futexByOp = mutableMapOf<Int, SyscallResult>()
    val futexSleepMs = mutableMapOf<Int, Long>()
    var processVmWritevResult: SyscallResult = SyscallResult.Ok(4L)
    var processVmReadvResult: SyscallResult = SyscallResult.Ok(4L)
    val vmPages = mutableMapOf<Long, ByteArray>()
    val mmapAddresses = ArrayDeque<Long>()
    private val answers = mutableMapOf<String, ArrayDeque<SyscallResult>>()
    var default: SyscallResult = SyscallResult.Ok(0L)

    fun script(method: String, result: SyscallResult) {
        answers[method] = ArrayDeque(listOf(result))
    }

    fun script(method: String, vararg results: SyscallResult) {
        answers[method] = ArrayDeque(results.toList())
    }

    private fun take(method: String): SyscallResult {
        val queue = answers[method]
        return if (queue != null && queue.isNotEmpty()) queue.removeFirst() else default
    }

    private fun reply(method: String): SyscallResult {
        calls += method
        return take(method)
    }

    override fun futex(uaddr: Long, op: Int, value: Int, timeoutNs: Long, uaddr2: Long, value3: Int): SyscallResult {
        calls += "futex"
        futexOps += op
        val delay = futexSleepMs[op]
        if (delay != null && delay > 0L) {
            try {
                Thread.sleep(delay)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                return SyscallResult.Errno(KernelErrno.EINTR)
            }
        }
        return futexByOp[op] ?: take("futex")
    }
    override fun mmap(length: Long, prot: Int, flags: Int, fd: Int, offset: Long): SyscallResult {
        calls += "mmap"
        return if (mmapAddresses.isNotEmpty()) SyscallResult.Ok(mmapAddresses.removeFirst()) else take("mmap")
    }
    override fun munmap(address: Long, length: Long) = reply("munmap")
    override fun memfdCreate(name: String, flags: Int) = reply("memfdCreate")
    override fun socket(domain: Int, type: Int, protocol: Int) = reply("socket")
    override fun setsockopt(fd: Int, level: Int, option: Int, value: ByteArray) = reply("setsockopt")
    override fun send(fd: Int, buffer: ByteArray, flags: Int) = reply("send")
    override fun recv(fd: Int, buffer: ByteArray, flags: Int) = reply("recv")
    override fun close(fd: Int) = reply("close")
    override fun read(fd: Int, buffer: ByteArray) = reply("read")
    override fun write(fd: Int, buffer: ByteArray) = reply("write")
    override fun perfEventOpen(attr: ByteArray, pid: Int, cpu: Int, groupFd: Int, flags: Long) =
        reply("perfEventOpen")
    override fun ioctl(fd: Int, request: Long, arg: Long) = reply("ioctl")
    override fun gettid() = reply("gettid")
    override fun getpid() = reply("getpid")
    override fun processVmWritev(pid: Int, address: Long, buffer: ByteArray): SyscallResult {
        calls += "processVmWritev"
        val target = vmPages[address]
        if (target != null) {
            val count = minOf(buffer.size, target.size)
            buffer.copyInto(target, 0, 0, count)
            return SyscallResult.Ok(count.toLong())
        }
        val queue = answers["processVmWritev"]
        return if (queue != null && queue.isNotEmpty()) queue.removeFirst() else processVmWritevResult
    }

    override fun processVmReadv(pid: Int, address: Long, buffer: ByteArray): SyscallResult {
        calls += "processVmReadv"
        val source = vmPages[address]
        if (source != null) {
            val count = minOf(buffer.size, source.size)
            source.copyInto(buffer, 0, 0, count)
            return SyscallResult.Ok(count.toLong())
        }
        val queue = answers["processVmReadv"]
        return if (queue != null && queue.isNotEmpty()) queue.removeFirst() else processVmReadvResult
    }
    override fun usleep(micros: Long) = reply("usleep")
    override fun clockGettimeNs(clockId: Int) = reply("clockGettimeNs")
    override fun pipe2(out: IntArray, flags: Int): SyscallResult {
        val result = reply("pipe2")
        if (result is SyscallResult.Ok) {
            out[0] = 3
            out[1] = 4
        }
        return result
    }
    override fun schedSetaffinity(pid: Int, cpuMask: Long) = reply("schedSetaffinity")
    override fun schedSetattr(tid: Int, attr: ByteArray) = reply("schedSetattr")
    override fun schedGetcpu() = reply("schedGetcpu")
    override fun rseq(rseq: ByteArray, flags: Int, signature: Int) = reply("rseq")
}
