package com.ghostlock.app.engine.ktstep

data class K3FixtureResult(
    val pageWritten: Boolean,
    val readbackOk: Boolean,
    val actors: PiActorsResult?,
    val fixtureSucceeded: Boolean,
    val refusal: String?,
)

/**
 * K3a 离线 boundary fixture：先把 32 KiB 页写进用户 mmap 并读回校验，再跑独立 actor words。
 *
 * 不返回 KernelRoute.Landed，不证明内核消费了页；actor words 与页是两套映射，
 * 因此这里只证明 “page write 成功后才启动 actors” 的顺序，不证明因果。
 */
class K3PageFixture(
    private val syscalls: KernelSyscalls,
    private val writer: PageWriter,
    private val config: PiActorsConfig = PiActorsConfig(requeueSettleMs = 500L),
) {
    fun run(page: ByteArray): K3FixtureResult {
        if (writer.kind != PageWriterKind.Diagnostic) {
            return K3FixtureResult(false, false, null, false, "writer-kind")
        }
        if (page.isEmpty()) return K3FixtureResult(false, false, null, false, "page-empty")
        val mapping = when (val mapped = syscalls.mmap(
            page.size.toLong(),
            KernelFutexWords.PROT_READ or KernelFutexWords.PROT_WRITE,
            KernelFutexWords.MAP_PRIVATE or KernelFutexWords.MAP_ANONYMOUS,
            -1,
            0L,
        )) {
            is SyscallResult.Ok -> mapped.value
            is SyscallResult.Errno -> return K3FixtureResult(false, false, null, false, "mmap:" + mapped.code)
        }
        val pid = when (val answer = syscalls.getpid()) {
            is SyscallResult.Ok -> answer.value.toInt()
            is SyscallResult.Errno -> {
                syscalls.munmap(mapping, page.size.toLong())
                return K3FixtureResult(false, false, null, false, "getpid:" + answer.code)
            }
        }

        when (val written = writer.write(mapping, page)) {
            is PageWriteResult.Written -> Unit
            is PageWriteResult.ShortWrote -> {
                syscalls.munmap(mapping, page.size.toLong())
                return K3FixtureResult(false, false, null, false, "short-write:" + written.size)
            }
            is PageWriteResult.Errno -> {
                syscalls.munmap(mapping, page.size.toLong())
                return K3FixtureResult(false, false, null, false, "write:" + written.code)
            }
        }

        val readback = ByteArray(page.size)
        val read = syscalls.processVmReadv(pid, mapping, readback)
        val readbackOk = read is SyscallResult.Ok && read.value == page.size.toLong() && readback.contentEquals(page)
        if (!readbackOk) {
            syscalls.munmap(mapping, page.size.toLong())
            return K3FixtureResult(true, false, null, false, "readback")
        }

        val words = when (val made = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Errno -> {
                syscalls.munmap(mapping, page.size.toLong())
                return K3FixtureResult(true, true, null, false, "words:" + made.code)
            }
            is FutexWordsResult.Ok -> made.words
        }
        val actors = KotlinPiActors(words.chain, words.target, words.wait, config).run()
        words.close()
        syscalls.munmap(mapping, page.size.toLong())
        val actorsOk = actors.completed && actors.waitResult == 0 && actors.requeueResult >= 1
        return K3FixtureResult(true, true, actors, actorsOk, if (actorsOk) null else "actors")
    }
}
