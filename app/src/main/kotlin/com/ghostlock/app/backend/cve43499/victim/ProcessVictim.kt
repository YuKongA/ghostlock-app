package com.ghostlock.app.backend.cve43499.victim

import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * 最小受害者进程 seam；[close] 拥有有界终结，不泄漏 child。
 *
 * @note 阶段：G4L-6 victim；实现必须是 Kotlin 进程（ProcessBuilder），不走 JNI。
 */
internal interface VictimProcess : AutoCloseable {
    /** @return 当前 uid；读失败返回 null。实现必须有界。 */
    fun uid(): Int?

    /** 有界、幂等的终结；重复调用是无副作用 no-op。 */
    override fun close()
}

/**
 * Kotlin 受害者子进程：一个 shell 循环，每收到一行就回答一次 `id -u`。
 *
 * 构造是原子的：任一 stream 创建失败会先把已启动的进程按有界顺序收掉，再把异常抛回。
 *
 * @param command 子进程命令；默认 `/system/bin/sh -c 'while read _; do id -u; done'`。
 * @param streamFactory stream 构造 seam（宿主验证可在进程创建后注入失败）。
 * @note 阶段：G4L-6 victim；不读 profile、不连 Native。
 */
internal class ProcessVictim(
    private val command: List<String> = DEFAULT_COMMAND,
    streamFactory: (Process) -> Streams = { process ->
        Streams(process.outputStream.bufferedWriter(), process.inputStream.bufferedReader())
    },
) : VictimProcess {
    /** 与 victim 子进程交互的两条流。 */
    data class Streams(val input: BufferedWriter, val output: BufferedReader)

    private val process: Process
    private val input: BufferedWriter
    private val output: BufferedReader

    init {
        val started = ProcessBuilder(command).redirectErrorStream(true).start()
        try {
            val streams = streamFactory(started)
            process = started
            input = streams.input
            output = streams.output
        } catch (problem: Throwable) {
            started.destroy()
            if (!started.waitFor(CLOSE_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                started.destroyForcibly()
            }
            throw problem
        }
    }

    /**
     * 发送一次探测并读取一行 uid。
     *
     * @return uid；I/O 失败返回 null。
     * @note 与 [close] 用同一把对象锁串行，避免探测与关闭竞争同一对流。
     */
    /** @return 子进程 PID；用于 victim 身份绑定与 starttime 复查。 */
    fun pid(): Int = process.pid().toInt()

    @Synchronized
    override fun uid(): Int? = try {
        input.write("probe\n")
        input.flush()
        output.readLine()?.trim()?.toIntOrNull()
    } catch (failed: IOException) {
        null
    }

    /** 有界、幂等地关闭流并回收子进程；忽略已退出/destroy 抛出的异常。 */
    override fun close() {
        runCatching { input.close() }
        runCatching { output.close() }
        runCatching { process.destroy() }
        try {
            if (!process.waitFor(CLOSE_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                runCatching { process.destroyForcibly() }
                process.waitFor(CLOSE_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            }
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    companion object {
        /** 默认 victim 命令：固定字符串，不接受调用方拼接。 */
        internal val DEFAULT_COMMAND: List<String> =
            listOf("/system/bin/sh", "-c", "while read _; do id -u; done")

        /** 关闭/强杀的有界等待（毫秒）。 */
        internal const val CLOSE_TIMEOUT_MS: Long = 500L
    }
}

/**
 * 生产形状的 uid 读回口：uid 来自 Kotlin 子进程，不来自 JNI。
 *
 * @param victim 已启动的受害者进程（调用方拥有其生命周期）。
 * @note 阶段：G4L-6 victim。
 */
internal class ProcessVictimProbe(private val victim: ProcessVictim) : VictimProbe {
    override val kind: VictimProbeKind = VictimProbeKind.PRODUCTION

    override fun uid(): VictimUidResult {
        val value = victim.uid() ?: return VictimUidResult.Failed("victim-probe:epipe")
        return VictimUidResult.Ok(value.toLong())
    }
}
