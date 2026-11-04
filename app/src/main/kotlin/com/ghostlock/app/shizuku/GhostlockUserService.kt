package com.ghostlock.app.shizuku

import android.content.Context
import android.os.Bundle
import android.os.ParcelFileDescriptor
import androidx.annotation.Keep
import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.backend.host.HostRunPaths
import com.ghostlock.app.backend.host.cleanupHostPaths
import com.ghostlock.app.backend.host.createShellHostPaths
import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.backend.declarations.BackendRegistryIdentityFactory
import com.ghostlock.app.backend.declarations.FlowEntryRegistryAssembly
import com.ghostlock.app.backend.declarations.BackendSpec
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
import com.ghostlock.app.shizuku.host.ShizukuBackendHost
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

/**
 * **全 backend 共用的 Shizuku UserService 启动器/host**。
 *
 * Shizuku 通过 `bindUserService` 在 **uid 2000 / u:r:shell:s0 独立进程**（app_process）实例化本类；
 * 本类只做通用层：AIDL stub、backend host 分发、日志泵、日志 fd、生命周期。
 * backend 专用逻辑在 `ShizukuBackendHost` 实现（legacy backend host；新通道走 Flow AIDL）。
 *
 * @note 不再存在任何 per-backend UserService/AIDL（2026-10-28 用户裁定：启动器必须共用）。
 * @note 未在 manifest 注册（Shizuku UserService 非 AMS 组件）；真机门禁 G8–G14 pending。
 */
class GhostlockUserService : IGhostlockUserService.Stub {
    /** Shell-process immutable registry membership captured once by this UserService root. */
    private val backendRegistryEntries: List<BackendSpec> =
        Backends.all.toList()

    /** Shell-process identity calculated from the same captured membership as [hostRegistry]. */
    private val backendRegistryIdentity: BackendRegistryIdentity =
        BackendRegistryIdentityFactory.capture(backendRegistryEntries)

    /** Shell-process immutable Flow allowlist assembled once from [backendRegistryEntries]. */
    private val hostRegistry: FlowEntryRegistry by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        FlowEntryRegistryAssembly.hostRegistry(backendRegistryEntries)
    }

    @Volatile
    private var userServiceContext: Context? = null

    private val pumping = AtomicBoolean(false)
    private val consumed = AtomicLong(0L)
    private var pumpThread: Thread? = null

    @Volatile
    private var host: ShizukuBackendHost? = null

    @Volatile
    private var callback: IGhostlockCallback? = null

    // ---- B3a：通用 FlowHost 分支状态（与 app FlowHostService 语义一致） ----

    /** Flow 状态机（FlowHostProtocol IDLE/PREPARED/RUNNING/TERMINAL）。 */
    private val flowState = AtomicInteger(FlowHostProtocol.IDLE)

    /** Flow 取消标志（入口通过 FlowHostEnv.isCancelled 观察）。 */
    private val flowCancelled = AtomicBoolean(false)

    /** 保护 flow 状态迁移/日志缓冲（不在锁内执行 Flow）。 */
    private val flowLock = Any()

    /** 已解析入口（prepareFlow 成功后非空）。 */
    @Volatile
    private var flowEntry: FlowEntrySpec? = null

    /** 已校验 Flow 参数。 */
    @Volatile
    private var flowParams: FlowHostParams? = null

    /** Flow payload 深拷贝；null 表示未提供。 */
    @Volatile
    private var flowPayload: ByteArray? = null

    /** Flow 终态 verify 字节。 */
    @Volatile
    private var flowVerify: ByteArray? = null

    /** Flow 内存日志（受 flowLock 保护）。 */
    private val flowLogText = StringBuilder()

    /** 可选 logFd 写入流。 */
    @Volatile
    private var flowLogStream: FileOutputStream? = null

    /** Flow 后台执行线程（一次性）。 */
    @Volatile
    private var flowWorker: Thread? = null

    /** 本次 Flow run 的 shell 目录/路径事实（prepareFlow 成功后非空）。 */
    @Volatile
    private var flowRunPaths: HostRunPaths? = null

    /** Flow 分支最近一次具名拒绝原因（lastReason() 回退展示）。 */
    @Volatile
    private var flowLastReason: String? = null

    /** A11 资源保全：Flow 超时/取消后保活 shell 进程（持有 runtime/资源），不清理不退出。 */
    @Volatile
    private var flowKeepAliveActive: Boolean = false

    /** 默认构造器（Shizuku 必需）。 */
    constructor()

    /**
     * API v13+ 的 Context 构造器（Shizuku 优先尝试）；必须 @Keep。
     *
     * @param context 由 Shizuku 创建的 package context（不是有效 app 进程的 Context）。
     */
    @Keep
    constructor(context: Context) {
        userServiceContext = context
    }

    override fun setCallback(cb: IGhostlockCallback?) {
        callback = cb
    }

    override fun clearCallback() {
        callback = null
    }

    override fun prepare(backendId: String?, config: Bundle?, payload: ByteArray?): Int {
        if (backendId == null || config == null || payload == null) {
            return GENERIC_ERR_ARGS
        }
        val active = host
        if (active != null && active.state() != HOST_STATE_IDLE) {
            return GENERIC_ERR_BUSY
        }
        val created = ShizukuBackendHosts.create(backendId, userServiceContext)
            ?: return GENERIC_ERR_UNSUPPORTED
        host = created
        return created.prepare(config, payload)
    }

    override fun start(): Int {
        val active = host ?: return -GENERIC_ERR_STATE
        val result = active.start()
        if (result > 0) startPump()
        return result
    }

    override fun getState(): Int = host?.state() ?: HOST_STATE_IDLE

    override fun readLog(offset: Long, maxBytes: Int): ByteArray =
        host?.readLog(offset, maxBytes) ?: ByteArray(0)

    override fun verify(): ByteArray = host?.verify() ?: ByteArray(0)

    override fun bootId(): String = host?.bootId() ?: ""

    override fun ping(): Int = 1

    override fun lastReason(): String = host?.lastReason() ?: flowLastReason ?: ""

    override fun attachLog(logFd: ParcelFileDescriptor?) {
        if (logFd == null) return
        val raw = runCatching { logFd.detachFd() }.getOrElse { return }
        /* true means the host owns raw; false leaves ownership here for close (AIDL onLog remains). */
        val attached = host?.attachLogFd(raw) ?: false
        if (!attached) {
            runCatching { android.os.ParcelFileDescriptor.adoptFd(raw).close() }
        }
    }

    // ---- B3a：通用 FlowHost 分支（新方法；旧 backend host 方法保持不变） ----

    /**
     * 准备一次 Flow run。
     *
     * @param symbol 反射符号（只命中内置诊断 allowlist；B4 起换 BackendSpec registry）。
     * @param params 参数 Bundle（null 视为空）。
     * @param payload 可选 payload（≤512 KiB）。
     * @param logFd 可选日志 fd。
     * @return [FlowHostProtocol] 稳定错误码。
     */
    override fun prepareFlow(
        symbol: String?,
        params: Bundle?,
        payload: ByteArray?,
        logFd: ParcelFileDescriptor?,
    ): Int = synchronized(flowLock) {
        val current = flowState.get()
        if (current != FlowHostProtocol.IDLE) {
            return@synchronized if (current == FlowHostProtocol.TERMINAL) {
                FlowHostProtocol.ERR_BUSY
            } else {
                FlowHostProtocol.ERR_STATE
            }
        }
        val parsed = FlowSymbols.parse(symbol.orEmpty())
            ?: return@synchronized FlowHostProtocol.ERR_SYMBOL
        val registry = runCatching { hostRegistry }
            .getOrElse { return@synchronized FlowHostProtocol.ERR_SYMBOL }
        val spec = when (val resolution = registry.resolve(parsed)) {
            is FlowEntryResolution.Resolved -> resolution.entry
            is FlowEntryResolution.Rejected -> return@synchronized FlowHostProtocol.ERR_SYMBOL
        }
        val decoded = try {
            FlowHostParamsCodec.decode(params ?: Bundle())
        } catch (problem: IllegalArgumentException) {
            return@synchronized FlowHostProtocol.ERR_ARGS
        }
        if (payload != null && payload.size > FlowHostProtocol.MAX_PAYLOAD_BYTES) {
            flowLastReason = "flow-params:too-large-payload"
            return@synchronized FlowHostProtocol.ERR_ARGS
        }
        try {
            FlowEntryParams.validateUserParams(decoded.values())
        } catch (problem: IllegalArgumentException) {
            flowLastReason = problem.message ?: "flow-params:reserved-key:unknown"
            return@synchronized FlowHostProtocol.ERR_ARGS
        }
        val runId = "run-" + System.nanoTime().toString(36)
        val nativeLibAvailable =
            userServiceContext?.applicationInfo?.nativeLibraryDir?.isNotBlank() == true
        val paths = createShellHostPaths(userServiceContext, runId)
        if (paths == null) {
            flowLastReason = if (nativeLibAvailable) {
                "flow-host:run-dir:create"
            } else {
                "flow-host:native-lib-dir-unavailable"
            }
            return@synchronized FlowHostProtocol.ERR_ARGS
        }
        val injected = try {
            FlowEntryParams.injectHost(decoded, paths.toHostParams())
        } catch (problem: IllegalArgumentException) {
            cleanupHostPaths(paths)
            flowLastReason = problem.message ?: "flow-params:inject"
            return@synchronized FlowHostProtocol.ERR_ARGS
        }
        flowEntry = spec
        flowParams = injected
        flowRunPaths = paths
        flowLastReason = null
        flowPayload = payload?.copyOf()
        flowVerify = null
        flowKeepAliveActive = false
        flowCancelled.set(false)
        synchronized(flowLock) { flowLogText.setLength(0) }
        if (logFd != null) {
            flowLogStream = runCatching { FileOutputStream(logFd.fileDescriptor) }.getOrNull()
        }
        flowState.set(FlowHostProtocol.PREPARED)
        FlowHostProtocol.OK
    }

    /**
     * 启动已准备的 Flow run（后台线程执行）。
     *
     * @return [FlowHostProtocol] 稳定错误码。
     */
    override fun startFlow(): Int = synchronized(flowLock) {
        if (flowState.get() != FlowHostProtocol.PREPARED) {
            return@synchronized FlowHostProtocol.ERR_STATE
        }
        flowCancelled.set(false)
        flowState.set(FlowHostProtocol.RUNNING)
        val thread = Thread({ runFlowEntry() }, "flowhost-shizuku-worker")
        flowWorker = thread
        return@synchronized try {
            thread.start()
            FlowHostProtocol.OK
        } catch (problem: Throwable) {
            flowState.set(FlowHostProtocol.TERMINAL)
            FlowHostProtocol.ERR_START
        }
    }

    /** @return FlowHostProtocol 状态。 */
    override fun getFlowState(): Int = flowState.get()

    /** @return 当前 shell 进程 pid。 */
    override fun getFlowPid(): Int = android.os.Process.myPid()

    /** @return 终态 verify 字节（含 host 证据）；未终态空数组。 */
    override fun verifyFlow(): ByteArray = flowVerify?.copyOf() ?: ByteArray(0)

    /** 请求取消 Flow（幂等）。 */
    override fun cancelFlow() {
        flowCancelled.set(true)
    }

    /**
     * 读取 Flow 内存日志片段。
     *
     * @param offset 起始偏移（≥0）。
     * @param maxBytes 最大字节（1..MAX_LOG_BYTES）。
     * @return 片段；越界返回空数组。
     */
    override fun readFlowLog(offset: Long, maxBytes: Int): ByteArray {
        if (offset < 0L || maxBytes <= 0) return ByteArray(0)
        val snapshot = synchronized(flowLock) {
            flowLogText.toString().toByteArray(Charsets.UTF_8)
        }
        if (offset >= snapshot.size.toLong()) return ByteArray(0)
        val start = offset.toInt()
        val length = minOf(maxBytes, FlowHostProtocol.MAX_LOG_BYTES, snapshot.size - start)
        return snapshot.copyOfRange(start, start + length)
    }

    /** @return Shell-process registry content version captured before Flow resolution. */
    override fun getFlowRegistryVersion(): Long = backendRegistryIdentity.version

    /** @return Shell-process registry content hash captured before Flow resolution. */
    override fun getFlowRegistryContentHash(): String = backendRegistryIdentity.contentHash

    /** 后台执行 Flow 入口并落终态（不抛到线程外）。 */
    private fun runFlowEntry() {
        val spec = flowEntry
        val result = if (spec == null) {
            FlowResult.Failed(
                reason = "flow-host:entry-missing",
                failedStep = null,
                steps = emptyList(),
                evidence = emptyMap(),
            )
        } else {
            try {
                spec.invoke(flowEnvironment())
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
        val keepAlive = result.evidence[com.ghostlock.app.backend.host.HostProcessExec.EVIDENCE_HOST_KEEPALIVE] ==
            com.ghostlock.app.backend.host.HostProcessExec.EVIDENCE_KEEPALIVE_TRUE
        val enriched = withHostEvidence(result, terminalFlowEvidence(cleanup = !keepAlive))
        val encoded = runCatching { FlowHostProtocol.encodeVerify(enriched, handoff) }
            .getOrElse { problem ->
                FlowHostProtocol.encodeVerify(
                    FlowResult.Failed(
                        reason = "flow-host:verify-encode:" + problem::class.java.simpleName,
                        failedStep = null,
                        steps = emptyList(),
                        evidence = hostEvidence(),
                    ),
                )
            }
        flowVerify = encoded
        flushFlowLog()
        flowState.set(FlowHostProtocol.TERMINAL)
        val code = when {
            handoff -> FlowHostProtocol.TERMINAL_CODE_HANDOFF
            result is FlowResult.Ok -> FlowHostProtocol.TERMINAL_CODE_OK
            else -> FlowHostProtocol.TERMINAL_CODE_FAILED
        }
        runCatching { callback?.onComplete(code) }
        if (keepAlive) {
            /* A11：不 kill 载荷；shell host 保活并保留 run_dir/runtime，交由上层处置。 */
            flowKeepAliveActive = true
        }
    }

    /**
     * 给 FlowResult 追加 host 证据（host_uid/host_pid/host_domain）。
     *
     * @param result 原 Flow 结果。
     * @return 追加证据后的等价结果。
     */
    private fun withHostEvidence(
        result: FlowResult,
        extra: Map<String, String> = emptyMap(),
    ): FlowResult {
        val evidence = result.evidence + hostEvidence() + extra
        return when (result) {
            is FlowResult.Ok -> FlowResult.Ok(steps = result.steps, evidence = evidence)
            is FlowResult.Failed -> FlowResult.Failed(
                reason = result.reason,
                failedStep = result.failedStep,
                steps = result.steps,
                evidence = evidence,
            )
        }
    }

    /**
     * 生成终态 Flow 证据（shell run 路径 + 清理结果）；清理在编码前执行。
     *
     * @return `host_run_dir` 等字符串证据。
     */
    private fun terminalFlowEvidence(cleanup: Boolean = true): Map<String, String> {
        val paths = flowRunPaths
        val cleanupStatus = when {
            paths == null -> "none"
            !cleanup -> "skipped"
            cleanupHostPaths(paths) -> "ok"
            else -> "failed"
        }
        if (paths == null) return mapOf("host_run_dir_cleanup" to cleanupStatus)
        return linkedMapOf(
            "host_run_dir" to paths.runDir.absolutePath,
            "host_run_dir_cleanup" to cleanupStatus,
            "host_native_lib_dir" to paths.nativeLibDir,
            "host_log_path" to paths.logPath.absolutePath,
        )
    }

    /** @return shell 进程身份证据（uid/pid/SELinux domain）。 */
    private fun hostEvidence(): Map<String, String> = linkedMapOf(
        "host_uid" to android.os.Process.myUid().toString(),
        "host_pid" to android.os.Process.myPid().toString(),
        "host_domain" to readSelfDomain(),
    )

    /** @return /proc/self/attr/current 的 SELinux domain（失败 "unknown"）。 */
    private fun readSelfDomain(): String = runCatching {
        java.io.File("/proc/self/attr/current").readText()
            .trim { it == '\u0000' || it.isWhitespace() }
    }.getOrElse { "unknown" }

    /** @return Flow 运行环境（params/payload/log/cancel）。 */
    private fun flowEnvironment(): FlowHostEnv = object : FlowHostEnv {
        override val params: FlowHostParams = flowParams ?: FlowHostParams.of(emptyMap())
        override val context: android.content.Context? = userServiceContext

        override val payload: ByteArray? = this@GhostlockUserService.flowPayload?.copyOf()

        override fun log(line: String) {
            appendFlowLog(line)
        }

        override fun isCancelled(): Boolean = flowCancelled.get()
    }

    /**
     * 追加 Flow 日志（内存 + logFd + 既有回调 onLog）。
     *
     * @param line 日志正文（不得含密钥）。
     */
    private fun appendFlowLog(line: String) {
        synchronized(flowLock) { flowLogText.append(line).append('\n') }
        runCatching {
            flowLogStream?.apply {
                write(line.toByteArray(Charsets.UTF_8))
                write('\n'.code)
                flush()
            }
        }
        runCatching { callback?.onLog(line) }
    }

    /** 刷新/关闭 Flow logFd 写入流（幂等）。 */
    private fun flushFlowLog() {
        val stream = flowLogStream
        flowLogStream = null
        if (stream != null) runCatching { stream.close() }
    }

    override fun destroy() {
        flowCancelled.set(true)
        if (flowKeepAliveActive) {
            /* A11 保活路径：拒绝退出（保留 runtime/run_dir），只释放日志流。 */
            flushFlowLog()
            return
        }
        flushFlowLog()
        flowRunPaths?.let { cleanupHostPaths(it) }
        flowRunPaths = null
        stopPump()
        runCatching { host?.destroy() }
        host = null
        System.exit(0)
    }

    /** 通用日志泵：host 日志增量 → 回调（终端）+ host native 日志行（项目 log fd）。 */
    private fun startPump() {
        if (pumping.getAndSet(true)) return
        pumpThread = Thread({
            var carry = ""
            while (pumping.get()) {
                val active = host
                if (active == null) break
                val chunk = active.readLog(consumed.get(), MAX_CHUNK)
                if (chunk.isEmpty()) {
                    if (active.state() != HOST_STATE_RUNNING) break
                    runCatching { Thread.sleep(PUMP_INTERVAL_MS) }
                    continue
                }
                consumed.addAndGet(chunk.size.toLong())
                val text = carry + String(chunk, Charsets.UTF_8)
                val parts = text.split('\n')
                carry = parts.last()
                for (index in 0 until parts.size - 1) {
                    val line = parts[index].take(MAX_LINE)
                    if (line.isEmpty()) continue
                    active.logLine(2, line)
                    callback?.onLog(line)
                }
            }
        }, "gl-host-log-pump").apply { isDaemon = true; start() }
    }

    private fun stopPump() {
        pumping.set(false)
        pumpThread?.let { runCatching { it.join(PUMP_JOIN_MS) } }
        pumpThread = null
    }

    private companion object {
        /** 通用（非 backend 语义）返回码：仅表达 host 分发层错误。 */
        private const val GENERIC_ERR_ARGS = 1
        private const val GENERIC_ERR_BUSY = 3
        private const val GENERIC_ERR_STATE = 7
        private const val GENERIC_ERR_UNSUPPORTED = 9

        /** host 状态：IDLE/RUNNING（与 backend 状态值对齐；通用层只用于泵停止条件）。 */
        private const val HOST_STATE_IDLE = 0
        private const val HOST_STATE_RUNNING = 2

        private const val MAX_CHUNK = 64 * 1024
        private const val MAX_LINE = 512
        private const val PUMP_INTERVAL_MS = 500L
        private const val PUMP_JOIN_MS = 1000L
    }
}
