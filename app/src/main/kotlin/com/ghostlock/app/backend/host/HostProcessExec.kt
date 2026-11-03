package com.ghostlock.app.backend.host

import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowHostParams
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * host 进程内执行上游 ELF 的共享工具（B4b；43499/43284 两个 entry 复用）。
 *
 * 契约：
 * - 有界 `waitFor(100ms)` 轮询 + [FlowHostEnv.isCancelled] 检查；
 * - `redirectErrorStream(true)`，先起读线程再起（可选）stdin writer，writer 写完关闭；
 * - env 注入；超时/取消 **不 kill 载荷**（A11）→ 返回具名 [Outcome]；
 * - 不吞行（读线程逐行 `env.log`），不持久化；纯 JVM 可测部分（超时解析/参数构造/映射）与
 *   进程执行分离。
 *
 * @note 无 Android/Binder 依赖；无可变全局。
 * @note 阶段：B4b。
 */
object HostProcessExec {
    /** 默认执行超时 600s。 */
    const val DEFAULT_TIMEOUT_MS: Long = 600_000L

    /** 超时上限 1h（防参数失控）。 */
    const val MAX_TIMEOUT_MS: Long = 3_600_000L

    /** 可选超时覆盖参数键。 */
    const val PARAM_TIMEOUT_KEY: String = "exec.timeout_ms"

    /** 取消检查粒度（ms）。 */
    const val WAIT_SLICE_MS: Long = 100L

    /** 终态 join 上限（ms；防读线程挂住）。 */
    const val JOIN_TIMEOUT_MS: Long = 2_000L

    /** host evidence：超时/取消后 host 必须保活（A11；保留 runtime/资源）。 */
    const val EVIDENCE_HOST_KEEPALIVE: String = "host_keepalive"

    /** host evidence keepalive 真值。 */
    const val EVIDENCE_KEEPALIVE_TRUE: String = "true"

    /**
     * 一次子进程执行的不可变描述。
     *
     * @property argv 完整 argv（argv[0]=二进制绝对路径）。
     * @property directory 工作目录（host.run_dir）。
     * @property environment 追加的 env（在继承环境上覆盖）。
     * @property stdin 可选 stdin 文件（null=立即关闭 stdin 让子进程见 EOF）。
     * @property timeoutMs 有界等待上限（>0）。
     */
    data class Spec(
        val argv: List<String>,
        val directory: File,
        val environment: Map<String, String>,
        val stdin: File?,
        val timeoutMs: Long,
    )

    /** 执行结局（超时/取消不 kill；载荷由上层/用户处置）。 */
    sealed interface Outcome {
        /** 子进程正常退出。 */
        data class Completed(val exitCode: Int) : Outcome

        /** 有界等待超时（进程未 kill，detach）。 */
        data class TimedOut(val timeoutMs: Long) : Outcome

        /** host 已请求取消（进程未 kill，detach）。 */
        data object Cancelled : Outcome

        /** 启动失败（可执行缺失/权限/参数非法等）。 */
        data class StartFailed(val reason: String) : Outcome

        /** stdin 写入失败（读线程已起；进程不 kill）。 */
        data class StdinFailed(val reason: String) : Outcome
    }

    /**
     * 解析本次执行超时（`exec.timeout_ms` 覆盖；<=0 或缺失 → 默认；上限 clamp）。
     *
     * @param params 已注入的 host 参数。
     * @return 实际使用的超时（ms）。
     */
    fun resolveTimeoutMs(params: FlowHostParams): Long {
        val override = params.getLong(PARAM_TIMEOUT_KEY) ?: params.getInt(PARAM_TIMEOUT_KEY)?.toLong()
        val value = override ?: DEFAULT_TIMEOUT_MS
        if (value <= 0L) return DEFAULT_TIMEOUT_MS
        return value.coerceAtMost(MAX_TIMEOUT_MS)
    }

    /**
     * 构造执行描述（argv=二进制绝对路径+arguments；env 复制；stdin 可选）。
     *
     * @param binary 目标 ELF（必须给出；存在性在调用方检查以便具名失败）。
     * @param arguments 追加参数。
     * @param directory 工作目录（host.run_dir）。
     * @param environment 追加 env。
     * @param stdin 可选 stdin 文件。
     * @param timeoutMs 超时（>0）。
     * @return 不可变 [Spec]。
     * @throws IllegalArgumentException argv 含 NUL 或 timeout<=0。
     */
    fun spec(
        binary: File,
        arguments: List<String>,
        directory: File,
        environment: Map<String, String>,
        stdin: File?,
        timeoutMs: Long,
    ): Spec {
        require(timeoutMs > 0L) { "flow-exec:timeout:non-positive" }
        val argv = buildList {
            add(binary.absolutePath)
            addAll(arguments)
        }
        require(argv.none { '\u0000' in it }) { "flow-exec:argv:nul" }
        return Spec(
            argv = argv,
            directory = directory,
            environment = LinkedHashMap(environment),
            stdin = stdin,
            timeoutMs = timeoutMs,
        )
    }

    /**
     * 纯映射：把执行事实映射为 [Outcome]
     * （优先级 start>cancelled>timeout>stdin>completed；取消/超时是终态，不被 stdin 失败覆盖）。
     *
     * @param exitCode 子进程退出码；未退出传 null。
     * @param timedOut 是否超时。
     * @param cancelled 是否取消。
     * @param startFailure 启动失败原因（非空即启动失败）。
     * @param stdinFailure stdin 失败原因。
     * @return 结局。
     */
    fun classify(
        exitCode: Int?,
        timedOut: Boolean,
        cancelled: Boolean,
        startFailure: String? = null,
        stdinFailure: String? = null,
        timeoutMs: Long = DEFAULT_TIMEOUT_MS,
    ): Outcome {
        if (startFailure != null) return Outcome.StartFailed(startFailure)
        if (cancelled) return Outcome.Cancelled
        if (timedOut) return Outcome.TimedOut(timeoutMs)
        if (stdinFailure != null) return Outcome.StdinFailed(stdinFailure)
        return Outcome.Completed(exitCode ?: -1)
    }

    /**
     * 执行子进程（阻塞直到终态/超时/取消；超时/取消不 kill）。
     *
     * @param spec 执行描述。
     * @param hostEnv host 环境（日志逐行 + 取消标志）。
     * @return 结局；不抛到调用方（取消只返回给上层具名失败）。
     */
    fun run(spec: Spec, hostEnv: FlowHostEnv): Outcome {
        if (hostEnv.isCancelled()) return Outcome.Cancelled
        val builder = ProcessBuilder(spec.argv)
            .directory(spec.directory)
            .redirectErrorStream(true)
        builder.environment().putAll(spec.environment)
        val process = try {
            builder.start()
        } catch (problem: Throwable) {
            return Outcome.StartFailed(problem::class.java.simpleName)
        }
        val reader = Thread({
            runCatching {
                process.inputStream.bufferedReader().useLines { lines ->
                    lines.forEach { line -> hostEnv.log(line) }
                }
            }
        }, "host-exec-stdout")
        reader.isDaemon = true
        reader.start()
        var stdinFailure: String? = null
        var stdinThread: Thread? = null
        val stdinFile = spec.stdin
        if (stdinFile == null) {
            runCatching { process.outputStream.close() }
        } else {
            stdinThread = Thread({
                runCatching {
                    process.outputStream.use { stream -> stream.write(stdinFile.readBytes()) }
                }.onFailure { problem -> stdinFailure = problem::class.java.simpleName }
            }, "host-exec-stdin")
            stdinThread.isDaemon = true
            stdinThread.start()
        }
        val deadlineNanos = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(spec.timeoutMs)
        while (process.isAlive) {
            if (hostEnv.isCancelled()) {
                return classify(
                    exitCode = null,
                    timedOut = false,
                    cancelled = true,
                    stdinFailure = stdinFailure,
                )
            }
            val remainingMs = TimeUnit.NANOSECONDS.toMillis(deadlineNanos - System.nanoTime())
            if (remainingMs <= 0L) {
                return classify(
                    exitCode = null,
                    timedOut = true,
                    cancelled = false,
                    stdinFailure = stdinFailure,
                    timeoutMs = spec.timeoutMs,
                )
            }
            runCatching {
                process.waitFor(minOf(WAIT_SLICE_MS, remainingMs), TimeUnit.MILLISECONDS)
            }
        }
        val exitCode = runCatching { process.exitValue() }.getOrDefault(-1)
        runCatching { reader.join(JOIN_TIMEOUT_MS) }
        stdinThread?.let { runCatching { it.join(JOIN_TIMEOUT_MS) } }
        return classify(
            exitCode = exitCode,
            timedOut = false,
            cancelled = false,
            stdinFailure = stdinFailure,
        )
    }
}
