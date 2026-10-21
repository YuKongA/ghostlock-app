package com.ghostlock.app.engine.ktstep

/** 生产页写端口：K4 实现内核页落点；当前 fail-closed，绝不写用户地址冒充成功。 */
class KernelPageWriter : PageWriter {
    override val kind: PageWriterKind = PageWriterKind.Production

    override fun write(sink: Long, page: ByteArray): PageWriteResult =
        PageWriteResult.Errno(KernelErrno.ENOSYS)
}

/** 生产 victim uid 读回：K4 接 child pipe；当前 fail-closed。 */
class ProductionVictimProbe : ProductionProbe {
    override val kind: ProbeKind = ProbeKind.Production

    override fun uid(): SyscallResult = SyscallResult.Errno(KernelErrno.ENOSYS)
}

/** 生产 route 的形状：只有 KernelPageWriter 能进入；当前 writer 未实现，故只会 Refused。 */
class KernelPageRoute(
    private val writer: KernelPageWriter,
) : ProductionRoute {
    override val kind: RouteKind = RouteKind.Production

    override fun attempt(
        request: WriteRequest,
        page: ByteArray,
        inputs: PayloadPageInputs,
    ): RouteResult = when (val result = writer.write(request.target, page)) {
        is PageWriteResult.Written -> RouteResult.Indeterminate("kernel-page-writer:not-implemented")
        is PageWriteResult.ShortWrote -> RouteResult.Refused("short-write:" + result.size)
        is PageWriteResult.Errno -> RouteResult.Refused("page:" + result.code)
    }
}
