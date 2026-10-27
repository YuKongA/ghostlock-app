package com.ghostlock.app.backend.cve43499.victim

import java.io.IOException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/**
 * victim 会话启动结果；reason 是用户可见/可检索的具名证据。
 */
internal sealed interface VictimStartResult {
    /** 会话已启动（进程已存在）。 */
    data object Ok : VictimStartResult

    /** 启动失败；[reason] 稳定可检索，[cause] 仅用于诊断。 */
    data class Failed(val reason: String, val cause: Throwable? = null) : VictimStartResult
}

/**
 * victim 生命周期：先 start，再 probe；close 必须覆盖成功/失败/取消全路径。
 *
 * @note 阶段：G4L-6 victim；本接口不做 Native 调用。
 */
internal interface VictimSession : VictimProbe, AutoCloseable {
    /**
     * 启动 victim 会话（幂等：已启动时返回 Ok）。
     *
     * @return 启动结果；失败必须已回收任何半启动资源。
     */
    fun start(): VictimUidResult

    /**
     * 启动并给出具名失败原因（默认映射；实现可覆盖以保留异常身份）。
     *
     * @return Ok 或 Failed(reason, cause)。
     */
    fun startDetailed(): VictimStartResult = try {
        when (val answer = start()) {
            is VictimUidResult.Ok -> VictimStartResult.Ok
            is VictimUidResult.Failed -> VictimStartResult.Failed(answer.reason)
        }
    } catch (problem: Throwable) {
        VictimStartResult.Failed(
            "victim-start:" + (problem.message ?: problem.javaClass.simpleName),
            problem,
        )
    }
}

/**
 * Kotlin 子进程 victim 会话：固定命令 + 有界 uid 探测 + 有界幂等关闭。
 *
 * @param command victim 子进程命令；默认 [ProcessVictim.DEFAULT_COMMAND]。
 * @param processFactory 进程工厂 seam（宿主验证可注入 fake）。
 * @param probeTimeoutMs 单次 uid 探测的超时；超时会关闭进程并使会话失效。
 * @note 构造不做 I/O；所有资源都在 [start] 之后由本对象独占。
 * @note 阶段：G4L-6 victim；不使用全局线程池，不注册 Cleaner。
 */
internal class ProcessVictimSession(
    private val command: List<String> = ProcessVictim.DEFAULT_COMMAND,
    private val processFactory: (List<String>) -> VictimProcess = { ProcessVictim(it) },
    private val probeTimeoutMs: Long = DEFAULT_PROBE_TIMEOUT_MS,
    private val statReader: (Int) -> String? = { pid ->
        runCatching { java.io.File("/proc/" + pid + "/stat").readText() }.getOrNull()
    },
) : VictimSession {
    private var victim: VictimProcess? = null
    private var closed: Boolean = false
    private val probeGate = Any()
    private val executor = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "ghostlock-victim-uid").apply { isDaemon = true }
    }

    override val kind: VictimProbeKind = VictimProbeKind.PRODUCTION

    /**
     * 启动 victim 子进程；幂等。
     *
     * @return Ok(0) 已启动；失败为具名 Errno 语义文本。
     * @note 已关闭的会话拒绝启动。
     */
    override fun start(): VictimUidResult {
        synchronized(probeGate) {
            if (closed) return VictimUidResult.Failed("victim-start:closed")
            if (victim != null) return VictimUidResult.Ok(0L)
            return try {
                victim = processFactory(command)
                VictimUidResult.Ok(0L)
            } catch (failed: IOException) {
                VictimUidResult.Failed("victim-start:epipe")
            } catch (failed: Throwable) {
                VictimUidResult.Failed(
                    "victim-start:" + (failed.message ?: failed.javaClass.simpleName),
                )
            }
        }
    }

    /**
     * 有界读取 victim uid。
     *
     * @return Ok(uid) / Failed(具名 reason)；超时或异常会关闭进程并使会话失效。
     * @note 一次探测一个 future；探测超时会 close 进程、cancel future、使会话关闭。
     */
    override fun uid(): VictimUidResult {
        synchronized(probeGate) {
            if (closed) return VictimUidResult.Failed("victim-probe:closed")
            val current = victim ?: return VictimUidResult.Failed("victim-probe:not-started")
            val future = executor.submit<Int?> { current.uid() }
            return try {
                val value = future.get(probeTimeoutMs, TimeUnit.MILLISECONDS)
                    ?: return VictimUidResult.Failed("victim-probe:epipe")
                VictimUidResult.Ok(value.toLong())
            } catch (timeout: TimeoutException) {
                runCatching { current.close() }
                future.cancel(true)
                executor.shutdownNow()
                closed = true
                victim = null
                VictimUidResult.Failed("victim-probe:timeout")
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                VictimUidResult.Failed("victim-probe:interrupted")
            } catch (problem: Throwable) {
                VictimUidResult.Failed("victim-probe:epipe")
            }
        }
    }

    /**
     * 读取 victim 身份证据：PID + `/proc/<pid>/stat` starttime + uid。
     *
     * @return 身份证据；会话未启动/关闭/读取失败返回 null（验收按缺失拒绝）。
     * @note starttime 解析：`stat` 的 comm 字段可能含空格/括号，因此从最后一个 ')'
     *       之后的第 19 个 token 起算（整体第 22 字段 = starttime）。
     */
    override fun identity(): VictimIdentity? {
        val current: VictimProcess = synchronized(probeGate) {
            if (closed) return null
            victim ?: return null
        }
        val pid = when (current) {
            is ProcessVictim -> current.pid()
            else -> return null
        }
        val statLine = runCatching { statReader(pid) }.getOrNull() ?: return null
        val closeParen = statLine.lastIndexOf(')')
        if (closeParen < 0 || closeParen + 2 >= statLine.length) return null
        val fields = statLine.substring(closeParen + 2).trim().split(' ')
        /* fields[0] 是第 3 字段 state；starttime 是第 22 字段 ⇒ 下标 19。 */
        if (fields.size <= 19) return null
        val startTimeTicks = fields[19].toLongOrNull() ?: return null
        val uid = when (val probe = uid()) {
            is VictimUidResult.Ok -> probe.uid
            is VictimUidResult.Failed -> return null
        }
        return VictimIdentity(pid = pid, startTimeTicks = startTimeTicks, uid = uid)
    }

    /**
     * 有界、幂等关闭会话：先关 victim 进程（其自身拥有有界终结），再停 executor。
     *
     * @note 重复调用无副作用；不抛异常（中断被恢复后继续收尾）。
     */
    override fun close() {
        synchronized(probeGate) {
            if (closed) return
            closed = true
            runCatching { victim?.close() }
            victim = null
            runCatching { executor.shutdownNow() }
        }
    }

    companion object {
        /** 单次 uid 探测默认超时（毫秒）；与 production victim plan 的 2000 ms 一致。 */
        internal const val DEFAULT_PROBE_TIMEOUT_MS: Long = 2_000L
    }
}
