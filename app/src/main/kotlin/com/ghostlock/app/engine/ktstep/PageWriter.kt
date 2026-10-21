package com.ghostlock.app.engine.ktstep

enum class PageWriterKind { Production, Diagnostic }

sealed interface PageWriteResult {
    data class Written(val size: Int) : PageWriteResult
    data class ShortWrote(val size: Long) : PageWriteResult
    data class Errno(val code: Int) : PageWriteResult
}

/** 页写端口：生产实现留给 K4；诊断实现只写用户 mmap，不得冒充内核写。 */
interface PageWriter {
    val kind: PageWriterKind
    fun write(sink: Long, page: ByteArray): PageWriteResult
}

/** 诊断页写：process_vm_writev 到调用方给的 sink；对 kernel 地址预期 EFAULT。 */
class ProcessVmPageWriter(
    private val syscalls: KernelSyscalls,
    private val pid: Int,
) : PageWriter {
    override val kind: PageWriterKind = PageWriterKind.Diagnostic

    override fun write(sink: Long, page: ByteArray): PageWriteResult {
        if (page.isEmpty()) return PageWriteResult.Errno(KernelErrno.EINVAL)
        return when (val result = syscalls.processVmWritev(pid, sink, page)) {
            is SyscallResult.Ok -> if (result.value == page.size.toLong()) {
                PageWriteResult.Written(page.size)
            } else {
                PageWriteResult.ShortWrote(result.value)
            }
            is SyscallResult.Errno -> PageWriteResult.Errno(result.code)
        }
    }
}
