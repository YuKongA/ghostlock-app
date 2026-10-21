package com.ghostlock.app.engine.ktstep

/** K0b 的可归档读数；不把 requeue 成功当成 root。 */
data class PiProbeReport(
    val mapping: Long?,
    val chainBefore: Int?,
    val chainAfter: Int?,
    val waitBefore: Int?,
    val targetAfter: Int?,
    val actors: PiActorsResult?,
    val close: SyscallResult?,
    val refusal: String?,
) {
    val pageWrite: Boolean get() = false
    val root: Boolean get() = false

    fun logLines(): List<String> = buildList {
        if (refusal != null) {
            add("k0b-refusal=" + refusal)
        } else {
            add("k0b-mmap=0x" + (mapping ?: 0L).toString(16))
            add("k0b-chain-before=" + (chainBefore ?: -1))
            add("k0b-chain-after=" + (chainAfter ?: -1))
            add("k0b-wait-before=" + (waitBefore ?: -1))
            add("k0b-target-after=" + (targetAfter ?: -1))
            add("k0b-waiter=" + (actors?.waiterResult ?: 0))
            add("k0b-wait=" + (actors?.waitResult ?: 0))
            add("k0b-owner=" + (actors?.ownerResult ?: 0))
            add("k0b-requeue=" + (actors?.requeueResult ?: 0))
            add("k0b-completed=" + (actors?.completed ?: false))
            add("k0b-timeout=" + (actors?.timedOut ?: false))
            add("k0b-close=" + (close ?: "none"))
        }
        add("k0b-page-write=" + pageWrite)
        add("k0b-root=" + root)
    }
}

/** K0b 设备前/真机都可调用的最小探针：建映射、跑三演员、关映射、出读数。 */
class PiProbeRunner(
    private val syscalls: KernelSyscalls,
    private val config: PiActorsConfig = PiActorsConfig(
        parkTimeoutNs = 2_000_000_000L,
        requeueSettleMs = 500L,
    ),
    private val phase: (String) -> Unit = {},
) {
    fun run(): PiProbeReport {
        val made = when (val result = KernelFutexWords.create(syscalls, waitTimeoutNs = -1L)) {
            is FutexWordsResult.Errno -> return PiProbeReport(null, null, null, null, null, null, null, "mmap:" + result.code)
            is FutexWordsResult.Ok -> result.words
        }
        val chainBefore = made.readChain()
        val waitBefore = made.readWait()
        val actors = KotlinPiActors(made.chain, made.target, made.wait, config, phase).run()
        val chainAfter = made.readChain()
        val targetAfter = made.readTarget()
        val close = made.close()
        return PiProbeReport(made.mapping, chainBefore, chainAfter, waitBefore, targetAfter, actors, close, null)
    }
}
