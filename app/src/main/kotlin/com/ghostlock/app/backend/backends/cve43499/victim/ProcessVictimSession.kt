package com.ghostlock.app.backend.backends.cve43499.victim

import com.ghostlock.app.core.victim.VictimIdentity

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
     * @return 是否曾观察到 uid==0（W2 成功后置位；rooted 会话在 closeAll 时只 detach）。
     * @note 默认 false（宿主 fake 可覆写）；生产实现由 uid 探测置位。
     */
    val rooted: Boolean get() = false

    /**
     * 启动 victim 会话（幂等：已启动时返回 Ok）。
     *
     * @return 启动结果；失败必须已回收任何半启动资源。
     */
    fun start(): VictimUidResult

    /**
     * 在 rooted victim 内执行 finish 脚本（R3f）。
     *
     * @param scriptPath 已冻结的脚本绝对路径（handoff；由实现校验/编码协议行）。
     * @param timeoutMs 有界等待（毫秒；必须 >0）。
     * @return 退出码+输出摘要或具名失败；实现必须不抛异常。
     * @note 默认实现返回 `victim-finish:unsupported`（宿主 fake 可只覆写需要的命令）。
     */
    fun finish(scriptPath: String, timeoutMs: Long): VictimFinishResult =
        VictimFinishResult.Failed("victim-finish:unsupported")

    /**
     * rooted victim 的 detach：请求驻留（park）后释放会话引用，**不 close 流/不 destroy**。
     *
     * @return true=park 请求已发出/进程已不存在；false=无法确认（调用方仍不得 kill）。
     * @note 默认实现返回 false（宿主 fake 可覆写）；只有 rooted 会话应调用本方法。
     */
    fun detachRooted(): Boolean = false

    /* G04/A2 append-only（既有 finish/park/bye/detach 语义不变）。 */
    /** @return victim self raw status；不支持/不可读 null（调用方 FAIL）。 */
    fun status(): ByteArray? = null
    /** @param marker ≤15B 安全 ASCII。 @return true=已写入 self comm。 */
    fun commSet(marker: String): Boolean = false
    /** @return victim self comm（len/bytes）；不支持 null。 */
    fun comm(): Pair<Int, ByteArray>? = null

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
 * Kotlin 子进程 victim 会话：固定命令 + 有界 uid 探测 + 有界 finish + rooted detach。
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
    /* task-84：identity 只读 `/proc/<pid>/status` Uid，不再发 probe 协议命令（无副作用）。 */
    private val statusUidReader: (Int) -> Long? = { pid ->
        runCatching {
            java.io.File("/proc/" + pid + "/status").readLines()
                .firstOrNull { it.startsWith("Uid:") }
                ?.removePrefix("Uid:")?.trim()?.split(Regex("\\s+"))?.firstOrNull()
                ?.toLongOrNull()
        }.getOrNull()
    },
) : VictimSession {
    private var victim: VictimProcess? = null
    private var closed: Boolean = false
    private var rootedFlag: Boolean = false
    private val probeGate = Any()
    private val executor = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "ghostlock-victim-uid").apply { isDaemon = true }
    }

    override val kind: VictimProbeKind = VictimProbeKind.PRODUCTION

    override val rooted: Boolean get() = rootedFlag

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
                if (value == 0) rootedFlag = true
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
     * 在 rooted victim 内执行 finish 脚本（R3f，task-45）。
     *
     * @param scriptPath handoff 冻结的脚本绝对路径。
     * @param timeoutMs 有界等待（毫秒；必须 >0）。
     * @return 退出码+输出 SHA-256 或具名失败。
     * @note 有界等待 = timeoutMs + [VictimProtocol.FINISH_DRAIN_GRACE_MS]；victim 内
     *   watchdog 到期会先 kill 脚本再打退出标记，session 侧余量只用于排空管道。
     * @note 超时/异常会关闭 victim 并使会话失效（fail-closed：不猜测脚本结果）。
     */
    override fun finish(scriptPath: String, timeoutMs: Long): VictimFinishResult {
        val commandLine = VictimProtocol.finishCommand(timeoutMs, scriptPath)
            ?: return VictimFinishResult.Failed(
                "victim-finish:script-path:" +
                    (VictimProtocol.validateScriptPath(scriptPath) ?: "timeout-ms"),
            )
        synchronized(probeGate) {
            if (closed) return VictimFinishResult.Failed("victim-finish:closed")
            val current = victim ?: return VictimFinishResult.Failed("victim-finish:not-started")
            val future = executor.submit<VictimFinishResult> { current.finish(commandLine) }
            return try {
                future.get(timeoutMs + VictimProtocol.FINISH_DRAIN_GRACE_MS, TimeUnit.MILLISECONDS)
            } catch (timeout: TimeoutException) {
                /* A1 delta：finish 超时只 detach/park，**不 close/kill victim**；
                 * 仅显式 closeAll（非 rooted）才允许终止。timeout 后 victim 存活。 */
                val parked = detachForFinishTimeoutLocked()
                VictimFinishResult.Failed(
                    if (parked) "victim-finish:timeout" else "victim-finish:timeout:park-unconfirmed",
                    retained = true,
                )
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                VictimFinishResult.Failed("victim-finish:interrupted")
            } catch (problem: Throwable) {
                VictimFinishResult.Failed("victim-finish:epipe")
            }
        }
    }

    /**
     * finish 超时路径的 detach：只 park/释放会话引用，**不 close 流/不 kill/destroy victim**。
     *
     * @return true=park 请求已发出；false=写入失败（进程仍不杀，证据标 unconfirmed）。
     * @note 与 [detachRooted] 不同：不要求 rooted（finish 超时可能发生在非 rooted 判定前），
     *   但同样绝不终止 victim（A1 delta/I2）。
     */
    private fun detachForFinishTimeoutLocked(): Boolean {
        val current = victim
        closed = true
        victim = null
        val parked = current?.let { runCatching { it.park() }.getOrDefault(false) } ?: true
        runCatching { executor.shutdownNow() }
        return parked
    }

    /**
     * rooted victim 的 detach：请求 victim 驻留后释放会话，**不 close 流/不 destroy**。
     *
     * @return true=park 已请求（或进程已不存在）；false=非 rooted/写入失败（仍不 kill）。
     * @note 用户指令（task-45）：rooted child 不杀；closeAll 只 detach。本方法会有意
     *   不关闭 streams/不 destroy，rooted child 与两条管道驻留到进程退出（技术债：
     *   由后续批次决定 parked child 的回收策略）。
     */
    override fun detachRooted(): Boolean {
        synchronized(probeGate) {
            if (closed) return false
            val current = victim
            if (!rootedFlag) {
                return false
            }
            closed = true
            victim = null
            val parked = current?.let { runCatching { it.park() }.getOrDefault(false) } ?: true
            runCatching { executor.shutdownNow() }
            return parked
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
        val pid = current.pid() ?: return null
        val statLine = runCatching { statReader(pid) }.getOrNull() ?: return null
        val closeParen = statLine.lastIndexOf(')')
        if (closeParen < 0 || closeParen + 2 >= statLine.length) return null
        val fields = statLine.substring(closeParen + 2).trim().split(' ')
        /* fields[0] 是第 3 字段 state；starttime 是第 22 字段 ⇒ 下标 19。 */
        if (fields.size <= 19) return null
        val startTimeTicks = fields[19].toLongOrNull() ?: return null
        /* task-84：identity 必须只读/幂等（原实现调用 uid() 会发协议命令）。 */
        val uid = statusUidReader(pid) ?: return null
        return VictimIdentity(pid = pid, startTimeTicks = startTimeTicks, uid = uid)
    }

    override fun status(): ByteArray? {
        val current = synchronized(probeGate) {
            if (closed) return null
            victim ?: return null
        }
        return runCatching { current.status() }.getOrNull()
    }

    override fun commSet(marker: String): Boolean {
        val current = synchronized(probeGate) {
            if (closed) return false
            victim ?: return false
        }
        return runCatching { current.commSet(marker) }.getOrDefault(false)
    }

    override fun comm(): Pair<Int, ByteArray>? {
        val current = synchronized(probeGate) {
            if (closed) return null
            victim ?: return null
        }
        return runCatching { current.comm() }.getOrNull()
    }

    /**
     * 有界、幂等关闭会话：先关 victim 进程（其自身拥有有界终结），再停 executor。
     *
     * @note 重复调用无副作用；不抛异常（中断被恢复后继续收尾）。
     */
    override fun close() {
        synchronized(probeGate) {
            closeLocked()
        }
    }

    /**
     * 关闭会话的内部实现（调用方必须持有 [probeGate]）。
     *
     * @note 先 best-effort 发 `bye`（让固定 shell 循环正常退出），再按既有有界顺序
     *   close 流/destroy；rooted 会话不得走本路径（见 [detachRooted]）。
     */
    private fun closeLocked() {
        if (closed) return
        closed = true
        runCatching { victim?.bye() }
        runCatching { victim?.close() }
        victim = null
        runCatching { executor.shutdownNow() }
    }

    companion object {
        /** 单次 uid 探测默认超时（毫秒）；与 production victim plan 的 2000 ms 一致。 */
        internal const val DEFAULT_PROBE_TIMEOUT_MS: Long = 2_000L
    }
}
