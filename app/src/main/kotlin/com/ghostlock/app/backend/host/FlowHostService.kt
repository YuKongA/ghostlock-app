package com.ghostlock.app.backend.host

import android.app.Service
import android.content.Intent
import android.os.Bundle
import android.os.IBinder
import android.os.ParcelFileDescriptor
import android.os.Process
import android.os.RemoteException
import android.util.Log
import com.ghostlock.app.backend.declarations.BackendRegistryIdentityFactory
import com.ghostlock.app.backend.declarations.FlowEntryRegistryAssembly
import com.ghostlock.app.backend.registry.Backends
import com.ghostlock.app.core.flow.FlowEntryResolution
import com.ghostlock.app.core.flow.FlowEntryRegistry
import com.ghostlock.app.core.flow.FlowEntrySpec
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowHostParams
import com.ghostlock.app.core.flow.FlowHostParamsCodec
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.FlowSymbols
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import com.ghostlock.app.flow.IFlowHost
import com.ghostlock.app.flow.IFlowHostCallback
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * `:flowhost` 独立进程内的通用 Flow 宿主（B2a）。
 *
 * 生命周期：`onBind` → prepare（symbol/params/payload 校验 + 一次性 run 准备）→
 * start（后台线程 invoke，绝不阻塞 Binder 线程）→ TERMINAL（写 verify → onTerminal →
 * 短延迟后 stopSelf + killProcess）→ destroy/系统回收。
 *
 * 安全边界：
 * - symbol 只能命中本进程内置诊断 allowlist（B4 起由 BackendSpec 生成的 registry 替换）；
 * - params 走 B1 白名单/预算；payload ≤512 KiB；单 run one-shot；
 * - 不在 Binder 线程执行 Flow；不使用可变全局；日志同时回传 callback 与 logFd。
 *
 * @note 阶段：B2a；设备行为（每次 run 新 pid、旧进程消失）由 B2b 门禁。
 */
class FlowHostService : Service() {
    /** Process-local immutable registry membership captured once by this host composition root. */
    private val backendRegistryEntries by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        Backends.all.toList()
    }

    /** Process-local identity calculated from the same captured membership as [hostRegistry]. */
    private val backendRegistryIdentity: BackendRegistryIdentity by lazy(
        LazyThreadSafetyMode.SYNCHRONIZED,
    ) {
        BackendRegistryIdentityFactory.capture(backendRegistryEntries)
    }

    /** Process-local immutable allowlist assembled once from [backendRegistryEntries]. */
    private val hostRegistry: FlowEntryRegistry by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        FlowEntryRegistryAssembly.hostRegistry(backendRegistryEntries)
    }

    /** 进程内状态机（IDLE→PREPARED→RUNNING→TERMINAL）；只由本 Service 实例持有。 */
    private val state = AtomicInteger(FlowHostProtocol.IDLE)

    /** 取消标志：Flow 入口通过 [FlowHostEnv.isCancelled] 观察。 */
    private val cancelled = AtomicBoolean(false)

    /** 保护 prepare/start/verify/日志缓冲的状态迁移（不在锁内执行 Flow）。 */
    private val lock = Any()

    /** 后台执行线程（一次性）。 */
    @Volatile
    private var worker: Thread? = null

    /** 已解析入口（prepare 成功后非空）。 */
    @Volatile
    private var entry: FlowEntrySpec? = null

    /** 已校验参数（prepare 成功后非空）。 */
    @Volatile
    private var hostParams: FlowHostParams? = null

    /** 本次 run 的 host 目录/路径事实（prepare 成功后非空；终态 best-effort 清理）。 */
    @Volatile
    private var hostRunPaths: HostRunPaths? = null

    /** A11 资源保全：超时/取消后必须保活进程（持有 runtime/资源），不清理/不退出。 */
    @Volatile
    private var keepAliveActive: Boolean = false

    /** payload 深拷贝；null 表示未提供。 */
    @Volatile
    private var payload: ByteArray? = null

    /** host 日志 fd（可选）。 */
    @Volatile
    private var logFd: ParcelFileDescriptor? = null

    /** 日志 fd 写入流；null 表示只走 callback/logcat。 */
    @Volatile
    private var logStream: FileOutputStream? = null

    /** 内存日志（受 [lock] 保护）。 */
    private val logText = StringBuilder()

    /** 终态 verify 字节；null 表示未终态。 */
    @Volatile
    private var verifyBytes: ByteArray? = null

    /** 最近一次具名失败原因（诊断用）。 */
    @Volatile
    private var lastReason: String? = null

    /** 过程回调（可为空）。 */
    @Volatile
    private var callback: IFlowHostCallback? = null

    /** Binder 实现（唯一实例）。 */
    private val binder = object : IFlowHost.Stub() {
        override fun setCallback(cb: IFlowHostCallback?) {
            callback = cb
        }

        override fun clearCallback() {
            callback = null
        }

        override fun prepare(
            symbol: String?,
            params: Bundle?,
            payload: ByteArray?,
            logFd: ParcelFileDescriptor?,
        ): Int = prepareRun(symbol, params, payload, logFd)

        override fun start(): Int = startRun()

        override fun getState(): Int = this@FlowHostService.state.get()

        override fun getPid(): Int = Process.myPid()

        override fun readLog(offset: Long, maxBytes: Int): ByteArray = readLogSlice(offset, maxBytes)

        override fun verify(): ByteArray = verifyBytes?.copyOf() ?: ByteArray(0)

        override fun cancel() {
            cancelled.set(true)
        }

        override fun getRegistryVersion(): Long = backendRegistryIdentity.version

        override fun getRegistryContentHash(): String = backendRegistryIdentity.contentHash

        override fun destroy() {
            destroyHost()
        }
    }

    /** @return 本 Service 的 Binder。 */
    override fun onBind(intent: Intent?): IBinder = binder

    /** 系统回收时清理本地资源（不 kill 进程）。 */
    override fun onDestroy() {
        closeLogFd()
        if (!keepAliveActive) {
            hostRunPaths?.let { cleanupHostPaths(it) }
            hostRunPaths = null
        }
        super.onDestroy()
    }

    /**
     * 准备一次 run（仅 IDLE 可准备）。
     *
     * @param symbol 反射符号文本。
     * @param params 参数 Bundle（null 视为空）。
     * @param payload 可选 payload。
     * @param logFd 可选日志 fd。
     * @return [FlowHostProtocol] 稳定错误码。
     */
    private fun prepareRun(
        symbol: String?,
        params: Bundle?,
        payload: ByteArray?,
        logFd: ParcelFileDescriptor?,
    ): Int = synchronized(lock) {
        val current = state.get()
        if (current != FlowHostProtocol.IDLE) {
            return@synchronized fail(
                if (current == FlowHostProtocol.TERMINAL) {
                    FlowHostProtocol.ERR_BUSY
                } else {
                    FlowHostProtocol.ERR_STATE
                },
                "flow-host:state:prepare:" + stateName(current),
            )
        }
        val parsed = FlowSymbols.parse(symbol.orEmpty())
            ?: return@synchronized fail(
                FlowHostProtocol.ERR_SYMBOL,
                "flow-host:symbol:malformed",
            )
        val registry = runCatching { hostRegistry }
            .getOrElse { problem ->
                return@synchronized fail(
                    FlowHostProtocol.ERR_SYMBOL,
                    "flow-host:registry:" + (problem.message ?: problem::class.java.simpleName),
                )
            }
        val spec = when (val resolution = registry.resolve(parsed)) {
            is FlowEntryResolution.Resolved -> resolution.entry
            is FlowEntryResolution.Rejected ->
                return@synchronized fail(FlowHostProtocol.ERR_SYMBOL, resolution.reason)
        }
        val decoded = try {
            FlowHostParamsCodec.decode(params ?: Bundle())
        } catch (problem: IllegalArgumentException) {
            return@synchronized fail(
                FlowHostProtocol.ERR_ARGS,
                problem.message ?: "flow-params:decode",
            )
        }
        if (payload != null && payload.size > FlowHostProtocol.MAX_PAYLOAD_BYTES) {
            return@synchronized fail(FlowHostProtocol.ERR_ARGS, "flow-host:payload-too-large")
        }
        try {
            FlowEntryParams.validateUserParams(decoded.values())
        } catch (problem: IllegalArgumentException) {
            return@synchronized fail(
                FlowHostProtocol.ERR_ARGS,
                problem.message ?: "flow-params:reserved-key:unknown",
            )
        }
        val runId = "run-" + System.nanoTime().toString(36)
        val paths = try {
            createAppHostPaths(this@FlowHostService, runId)
        } catch (problem: Throwable) {
            return@synchronized fail(
                FlowHostProtocol.ERR_ARGS,
                problem.message ?: "flow-host:run-dir:create",
            )
        }
        val injected = try {
            FlowEntryParams.injectHost(decoded, paths.toHostParams())
        } catch (problem: IllegalArgumentException) {
            cleanupHostPaths(paths)
            return@synchronized fail(
                FlowHostProtocol.ERR_ARGS,
                problem.message ?: "flow-params:inject",
            )
        }
        entry = spec
        hostParams = injected
        hostRunPaths = paths
        this.payload = payload?.copyOf()
        this.logFd = logFd
        this.lastReason = null
        openLogFd(logFd)
        state.set(FlowHostProtocol.PREPARED)
        FlowHostProtocol.OK
    }

    /**
     * 启动后台执行（仅 PREPARED 可启动）。
     *
     * @return [FlowHostProtocol] 稳定错误码。
     */
    private fun startRun(): Int = synchronized(lock) {
        if (state.get() != FlowHostProtocol.PREPARED) {
            return@synchronized fail(
                FlowHostProtocol.ERR_STATE,
                "flow-host:state:start:" + stateName(state.get()),
            )
        }
        cancelled.set(false)
        state.set(FlowHostProtocol.RUNNING)
        val thread = Thread({ runPreparedEntry() }, "flowhost-worker")
        worker = thread
        return@synchronized try {
            thread.start()
            FlowHostProtocol.OK
        } catch (problem: Throwable) {
            state.set(FlowHostProtocol.TERMINAL)
            fail(
                FlowHostProtocol.ERR_START,
                "flow-host:start:" + problem::class.java.simpleName,
            )
        }
    }

    /** 在后台线程执行入口并落终态（不抛到线程外）。 */
    private fun runPreparedEntry() {
        val spec = entry
        val result = if (spec == null) {
            FlowResult.Failed(
                reason = "flow-host:entry-missing",
                failedStep = null,
                steps = emptyList(),
                evidence = emptyMap(),
            )
        } else {
            try {
                spec.invoke(environment())
            } catch (problem: Throwable) {
                FlowResult.Failed(
                    reason = "flow-host:entry-exception:" + problem::class.java.simpleName,
                    failedStep = null,
                    steps = emptyList(),
                    evidence = emptyMap(),
                )
            }
        }
        val handoff = result is FlowResult.Ok &&
            result.evidence[FlowHostProtocol.HANDOFF_EVIDENCE_KEY] ==
            FlowHostProtocol.HANDOFF_EVIDENCE_VALUE
        val keepAlive = result.evidence[HostProcessExec.EVIDENCE_HOST_KEEPALIVE] ==
            HostProcessExec.EVIDENCE_KEEPALIVE_TRUE
        val hostEvidence = terminalHostEvidence(cleanup = !keepAlive)
        val enriched = withHostEvidence(result, hostEvidence)
        val encoded = runCatching { FlowHostProtocol.encodeVerify(enriched, handoff) }
            .getOrElse { problem ->
                FlowHostProtocol.encodeVerify(
                    FlowResult.Failed(
                        reason = "flow-host:verify-encode:" + problem::class.java.simpleName,
                        failedStep = null,
                        steps = emptyList(),
                        evidence = hostEvidence,
                    ),
                )
            }
        verifyBytes = encoded
        closeLogFd()
        state.set(FlowHostProtocol.TERMINAL)
        notifyTerminal(
            when {
                handoff -> FlowHostProtocol.TERMINAL_CODE_HANDOFF
                result is FlowResult.Ok -> FlowHostProtocol.TERMINAL_CODE_OK
                else -> FlowHostProtocol.TERMINAL_CODE_FAILED
            },
        )
        if (keepAlive) {
            /* A11：超时/取消不 kill 载荷；host 保活进程持有 runtime/资源，不做清理/退出。 */
            keepAliveActive = true
            return
        }
        runCatching { Thread.sleep(TERMINAL_GRACE_MS) }
        stopSelf()
        Process.killProcess(Process.myPid())
    }

    /**
     * 生成终态 host 证据（run 路径 + 清理结果）；清理在编码前执行。
     *
     * @return `host_run_dir` 等字符串证据。
     */
    private fun terminalHostEvidence(cleanup: Boolean = true): Map<String, String> {
        val paths = hostRunPaths
        val cleanupStatus = when {
            paths == null -> "none"
            !cleanup -> "skipped"
            cleanupHostPaths(paths) -> "ok"
            else -> "failed"
        }
        if (paths == null) return mapOf("host_run_dir_cleanup" to cleanupStatus)
        return linkedMapOf(
            "host_run_dir" to paths.runDir.absolutePath,
            "host_uid" to paths.uid.toString(),
            "host_pid" to paths.pid.toString(),
            "host_domain" to paths.domain,
            "host_native_lib_dir" to paths.nativeLibDir,
            "host_log_path" to paths.logPath.absolutePath,
            "host_run_dir_cleanup" to cleanupStatus,
        )
    }

    /** @return 在 FlowResult evidence 上追加 host 证据（host 优先）。 */
    private fun withHostEvidence(
        result: FlowResult,
        evidence: Map<String, String>,
    ): FlowResult {
        val merged = result.evidence + evidence
        return when (result) {
            is FlowResult.Ok -> FlowResult.Ok(steps = result.steps, evidence = merged)
            is FlowResult.Failed -> FlowResult.Failed(
                reason = result.reason,
                failedStep = result.failedStep,
                steps = result.steps,
                evidence = merged,
            )
        }
    }

    /** @return host 运行环境（日志/取消/参数/payload）。 */
    private fun environment(): FlowHostEnv = object : FlowHostEnv {
        override val params: FlowHostParams = hostParams
            ?: FlowHostParams.of(emptyMap())
        override val context: android.content.Context = this@FlowHostService

        override val payload: ByteArray? = this@FlowHostService.payload?.copyOf()

        override fun log(line: String) {
            appendLog(line)
        }

        override fun isCancelled(): Boolean = cancelled.get()
    }

    /**
     * 追加一行 host 日志（内存 + logFd + callback；主线程安全由锁/回调自身保证）。
     *
     * @param line 日志正文（不得含密钥）。
     */
    private fun appendLog(line: String) {
        synchronized(lock) {
            logText.append(line).append('\n')
        }
        runCatching {
            logStream?.apply {
                write(line.toByteArray(Charsets.UTF_8))
                write('\n'.code)
                flush()
            }
        }.onFailure { problem ->
            Log.w(TAG, "cannot write host log fd", problem)
            lastReason = "flow-host:log-io:" + problem::class.java.simpleName
        }
        runCatching { callback?.onLine(line) }
            .onFailure { problem -> Log.w(TAG, "callback onLine failed", problem) }
    }

    /**
     * 读取内存日志片段。
     *
     * @param offset 起始字节偏移（≥0）。
     * @param maxBytes 最大字节数（1..[FlowHostProtocol.MAX_LOG_BYTES]）。
     * @return 片段副本；越界/非法参数返回空数组。
     */
    private fun readLogSlice(offset: Long, maxBytes: Int): ByteArray {
        if (offset < 0L || maxBytes <= 0) return ByteArray(0)
        val snapshot = synchronized(lock) { logText.toString().toByteArray(Charsets.UTF_8) }
        if (offset >= snapshot.size.toLong()) return ByteArray(0)
        val start = offset.toInt()
        val length = minOf(maxBytes, FlowHostProtocol.MAX_LOG_BYTES, snapshot.size - start)
        return snapshot.copyOfRange(start, start + length)
    }

    /**
     * 终结宿主：取消执行、关闭 fd、停止 service 并退出进程（幂等）。
     */
    private fun destroyHost() {
        cancelled.set(true)
        if (keepAliveActive) {
            /* A11 保活路径：只释放日志 fd，保留进程/runtime；不清理 run_dir、不 kill。 */
            closeLogFd()
            return
        }
        closeLogFd()
        hostRunPaths?.let { cleanupHostPaths(it) }
        hostRunPaths = null
        state.set(FlowHostProtocol.TERMINAL)
        stopSelf()
        Process.killProcess(Process.myPid())
    }

    /** 打开 logFd 写入流（失败只记日志，不打断 host）。 */
    private fun openLogFd(fd: ParcelFileDescriptor?) {
        logStream = if (fd == null) {
            null
        } else {
            runCatching { FileOutputStream(fd.fileDescriptor) }.getOrElse { problem ->
                Log.w(TAG, "cannot open log fd", problem)
                null
            }
        }
    }

    /** 关闭 logFd（幂等；不关闭调用方借用的 fd 本体，只关复制出来的流）。 */
    private fun closeLogFd() {
        val stream = logStream
        logStream = null
        if (stream != null) runCatching { stream.close() }
        logFd = null
    }

    /** @return 失败码（写 lastReason 后返回）。 */
    private fun fail(code: Int, reason: String): Int {
        lastReason = reason
        Log.w(TAG, "prepare/start failed: $reason")
        return code
    }

    /** @return 具名状态名（日志用）。 */
    private fun stateName(value: Int): String = when (value) {
        FlowHostProtocol.IDLE -> "idle"
        FlowHostProtocol.PREPARED -> "prepared"
        FlowHostProtocol.RUNNING -> "running"
        FlowHostProtocol.TERMINAL -> "terminal"
        else -> "unknown:$value"
    }

    /** 通知终态回调（oneway；失败只记日志）。 */
    private fun notifyTerminal(code: Int) {
        runCatching { callback?.onTerminal(code) }
            .onFailure { problem -> Log.w(TAG, "callback onTerminal failed", problem) }
    }

    private companion object {
        /** logcat TAG。 */
        const val TAG: String = "GLK-FlowHost"

        /** 终态后保留进程的宽限（让 Binder 回传/日志落地）。 */
        const val TERMINAL_GRACE_MS: Long = 300L
    }
}
