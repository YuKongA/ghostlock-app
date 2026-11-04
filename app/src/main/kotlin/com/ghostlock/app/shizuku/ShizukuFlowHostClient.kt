package com.ghostlock.app.shizuku

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.backend.host.HostProcessExec
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import kotlin.coroutines.coroutineContext
import rikka.shizuku.Shizuku
import java.util.concurrent.TimeUnit

/**
 * Shizuku UserService 侧的通用 FlowHost 客户端（B3a）。
 *
 * 职责：可用性预检 → 唯一 tag/进程后缀绑定 `GhostlockUserService` → prepareFlow/startFlow
 * → 状态轮询 → verifyFlow → 清理（cancel/destroy；unbind remove 由 keepalive 决定）；不 fallback。
 *
 * @note G13：绑定等待为可取消挂起轮询；terminal evidence `host_keepalive=true` ⇒
 *   `unbindUserService(remove=false)` 保留 shell 进程（A11），否则 remove=true。
 *
 * @param context 应用上下文（包名 + 可用性预检）。
 * @note 每次 [execute] 使用唯一 `tag`/`processNameSuffix`，使每次 run 由 Shizuku 起新 shell 进程；
 *   失败全部具名 `flow-launcher:shizuku:*`。
 * @note 阶段：B3a（设备 B3b 门禁；A301SO 无 Shizuku 包 ⇒ 本批仅编译/JVM）。
 */
internal class ShizukuFlowHostClient(
    context: Context,
    private val expectedRegistryIdentity: BackendRegistryIdentity? = null,
) {
    private val appContext: Context = context.applicationContext

    /** 本次 run 唯一 tag（Shizuku 用它区分 UserService 实例）。 */
    private val tag: String = "flowhost-" + System.nanoTime().toString(36)

    /** 本次 run 唯一进程名后缀（合法字符集内；每次 run 新进程）。 */
    private val processNameSuffix: String =
        "fh" + System.nanoTime().toString(36).takeLast(8)

    private val args: Shizuku.UserServiceArgs = Shizuku.UserServiceArgs(
        ComponentName(appContext.packageName, SERVICE_CLASS),
    )
        .daemon(false)
        .version(BuildConfig.VERSION_CODE)
        .tag(tag)
        .processNameSuffix(processNameSuffix)

    private val lock = Any()
    /** @return 当前绑定到的 UserService；未连接为 null。 */
    @Volatile
    private var service: IGhostlockUserService? = null

    /** @return 本次 run 的 shell 进程 pid（服务自报；失败 -1）。 */
    @Volatile
    var lastHostPid: Int = -1
        private set

    /** 日志行回调（各 run 独立设置）。 */
    @Volatile
    private var lineSink: ((String) -> Unit)? = null

    /** G14：shell host/binder 丢失标志（onServiceDisconnected/onBindingDied/onNullBinding）。 */
    @Volatile
    private var connectionLost: Boolean = false

    /** 绑定回调：保存 AIDL 接口；绑定等待是**可取消**挂起轮询（G13）。 */
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            connectionLost = false
            synchronized(lock) {
                service = binder?.let { IGhostlockUserService.Stub.asInterface(it) }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            synchronized(lock) { service = null }
            connectionLost = true
        }

        override fun onBindingDied(name: ComponentName?) {
            synchronized(lock) { service = null }
            connectionLost = true
        }

        override fun onNullBinding(name: ComponentName?) {
            synchronized(lock) { service = null }
            connectionLost = true
        }
    }

    /** UserService 回调：只把日志行转给本次 run 的 sink（终态靠轮询）。 */
    private val callback = object : IGhostlockCallback.Stub() {
        override fun onLog(line: String?) {
            runCatching { lineSink?.invoke(line.orEmpty()) }
        }

        override fun onStatus(state: Int) = Unit

        override fun onDropped(count: Int) {
            runCatching { lineSink?.invoke("flow-host:callback-dropped:$count") }
        }

        override fun onComplete(code: Int) {
            runCatching { lineSink?.invoke("flow-host:terminal-code:$code") }
        }
    }

    /**
     * 执行一次 Shizuku-host Flow run。
     *
     * @param symbol 反射符号（只命中内置诊断 allowlist）。
     * @param params 已编码 Bundle。
     * @param payload 可选 payload（≤512 KiB；由调用方预检）。
     * @param onLine 过程日志回调（Binder 线程）。
     * @return [ShizukuFlowHostOutcome]（Terminal=可映射；Failed=具名原因列表）。
     */
    internal suspend fun execute(
        symbol: String,
        params: Bundle,
        payload: ByteArray?,
        onLine: (String) -> Unit,
    ): ShizukuFlowHostOutcome = withContext(Dispatchers.IO) {
        lastHostPid = -1
        val preflight = availabilityReasons()
        if (preflight.isNotEmpty()) return@withContext ShizukuFlowHostOutcome.Failed(preflight)
        lineSink = onLine
        synchronized(lock) { service = null }
        connectionLost = false
        val bound = runCatching {
            Shizuku.bindUserService(args, connection)
        }.isSuccess
        if (!bound) return@withContext failed("flow-launcher:shizuku:bind-failed")
        try {
            if (!waitBound(BIND_TIMEOUT_MS)) {
                runCatching { Shizuku.unbindUserService(args, connection, true) }
                return@withContext failed("flow-launcher:shizuku:bind-timeout")
            }
        } catch (cancelled: CancellationException) {
            /* G13：绑定等待期间取消 ⇒ 立即 unbind 再传播取消（不再阻塞 10s）。 */
            runCatching { Shizuku.unbindUserService(args, connection, true) }
            throw cancelled
        }
        val host = synchronized(lock) { service }
            ?: return@withContext failed("flow-launcher:shizuku:bind-null")
        var keepAlive = false
        try {
            registryMismatch(host)?.let { reason ->
                return@withContext failed(reason)
            }
            runCatching { host.setCallback(callback) }
            val prepared = runCatching { host.prepareFlow(symbol, params, payload, null) }
                .getOrElse { problem ->
                    return@withContext failed(
                        "flow-launcher:shizuku:prepare-exception:" +
                            problem::class.java.simpleName,
                    )
                }
            if (prepared != FlowHostProtocol.OK) {
                return@withContext failed(
                    "flow-launcher:shizuku:prepare:" + codeName(prepared),
                    hostLogTail(host),
                )
            }
            lastHostPid = runCatching { host.getFlowPid() }.getOrDefault(-1)
            val started = runCatching { host.startFlow() }.getOrElse { problem ->
                return@withContext failed(
                    "flow-launcher:shizuku:start-exception:" + problem::class.java.simpleName,
                )
            }
            if (started != FlowHostProtocol.OK) {
                return@withContext failed(
                    "flow-launcher:shizuku:start:" + codeName(started),
                    hostLogTail(host),
                )
            }
            val terminal = when (val awaited = awaitTerminal(host)) {
                is ShizukuTerminalWait.Verify -> awaited.bytes
                    ?: return@withContext failed("flow-launcher:shizuku:timeout")

                ShizukuTerminalWait.Timeout ->
                    return@withContext failed("flow-launcher:shizuku:timeout")

                ShizukuTerminalWait.HostLost ->
                    return@withContext failed("flow-launcher:shizuku:host-lost")
            }
            val verified = runCatching { FlowHostProtocol.decodeVerify(terminal) }
                .getOrElse { problem ->
                    return@withContext failed(
                        "flow-launcher:shizuku:verify:" +
                            (problem.message ?: problem::class.java.simpleName),
                    )
                }
            keepAlive = verified.evidence[HostProcessExec.EVIDENCE_HOST_KEEPALIVE] ==
                HostProcessExec.EVIDENCE_KEEPALIVE_TRUE
            ShizukuFlowHostOutcome.Terminal(verified)
        } catch (cancelled: CancellationException) {
            /* G10：取消时请求 host.cancelFlow 后 rethrow（finally destroy/unbind 顺序不变）。 */
            runCatching { host.cancelFlow() }
            throw cancelled
        } catch (problem: Throwable) {
            failed("flow-launcher:shizuku:exception:" + problem::class.java.simpleName)
        } finally {
            lineSink = null
            runCatching { host.clearCallback() }
            runCatching { host.cancelFlow() }
            runCatching { host.destroy() }
            /* G13：keepalive（A11 runtime-leaked）时 remove=false 保留 shell 进程；其余 remove=true。 */
            runCatching { Shizuku.unbindUserService(args, connection, !keepAlive) }
        }
    }

    /**
     * 可取消绑定等待（25ms 轮询 + ensureActive）。
     *
     * @param timeoutMs 绑定上限（ms）。
     * @return true=超时前 service 已连接。
     * @throws kotlinx.coroutines.CancellationException 协程取消时立即抛出。
     */
    private suspend fun waitBound(timeoutMs: Long): Boolean {
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs)
        while (System.nanoTime() < deadline) {
            coroutineContext.ensureActive()
            if (synchronized(lock) { service } != null) return true
            delay(BIND_POLL_MS)
        }
        return synchronized(lock) { service } != null
    }

    /** G14：终态等待结果（区分正常超时与 host 丢失）。 */
    private sealed interface ShizukuTerminalWait {
        /** host 已 TERMINAL；verify 读取失败时 bytes=null（调用方按 timeout 处理）。 */
        data class Verify(val bytes: ByteArray?) : ShizukuTerminalWait

        /** 60s 总超时（行为不变）。 */
        data object Timeout : ShizukuTerminalWait

        /** shell host/binder 丢失（快速失败，具名 host-lost）。 */
        data object HostLost : ShizukuTerminalWait
    }

    /**
     * 轮询 flow 状态直到 TERMINAL。
     *
     * @param host 已启动的 UserService。
     * @return [ShizukuTerminalWait]；binder 断开/连续异常 ⇒ HostLost。
     */
    private suspend fun awaitTerminal(host: IGhostlockUserService): ShizukuTerminalWait {
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(RUN_TIMEOUT_MS)
        var consecutiveStateFailures = 0
        while (System.nanoTime() < deadline) {
            /* G10：可取消轮询（不再用阻塞 sleep 独占取消传播）。 */
            coroutineContext.ensureActive()
            /* G14：shell host/binder 丢失快速失败。 */
            if (connectionLost || synchronized(lock) { service } == null) {
                return ShizukuTerminalWait.HostLost
            }
            val stateResult = runCatching { host.getFlowState() }
            if (stateResult.isFailure) {
                consecutiveStateFailures += 1
                if (consecutiveStateFailures >= STATE_FAILURE_LIMIT) {
                    return ShizukuTerminalWait.HostLost
                }
            } else {
                consecutiveStateFailures = 0
                when (val current = stateResult.getOrNull()) {
                    FlowHostProtocol.TERMINAL ->
                        return ShizukuTerminalWait.Verify(
                            runCatching { host.verifyFlow() }.getOrNull(),
                        )

                    FlowHostProtocol.IDLE, FlowHostProtocol.PREPARED ->
                        return ShizukuTerminalWait.Timeout

                    else -> Unit
                }
            }
            delay(POLL_INTERVAL_MS)
        }
        return ShizukuTerminalWait.Timeout
    }

    /**
     * 读取 host 日志尾行（失败诊断；不含密钥）。
     *
     * @param host 当前 UserService。
     * @return 单行原因（无日志返回空列表）。
     */
    private fun hostLogTail(host: IGhostlockUserService): List<String> {
        val bytes = runCatching {
            host.readFlowLog(0L, FlowHostProtocol.MAX_LOG_BYTES)
        }.getOrNull() ?: return emptyList()
        if (bytes.isEmpty()) return emptyList()
        val line = bytes.toString(Charsets.UTF_8).trim().lineSequence().lastOrNull()
            ?: return emptyList()
        return listOf("flow-launcher:shizuku:host-log:" + line.take(200))
    }

    /** @return 可用性具名原因（`flow-launcher:shizuku:*`）；空=可用。 */
    private fun availabilityReasons(): List<String> {
        val probe = ShizukuBackendClient(appContext, "flowhost")
        return probe.availabilityReasons().map { reason ->
            "flow-launcher:shizuku:" + reason.removePrefix("shizuku:")
        }
    }

    /**
     * Compares the app-root registry identity with the shell host before prepareFlow.
     *
     * @param host Bound UserService Flow host.
     * @return Null when comparison is disabled or exact; otherwise a stable refusal reason.
     */
    private fun registryMismatch(host: IGhostlockUserService): String? {
        val expected = expectedRegistryIdentity ?: return null
        val actualVersion = runCatching { host.getFlowRegistryVersion() }.getOrElse {
            return "flow-launcher:shizuku:registry-version-unavailable"
        }
        if (actualVersion != expected.version) {
            return "flow-launcher:shizuku:registry-version-mismatch"
        }
        val actualHash = runCatching { host.getFlowRegistryContentHash() }.getOrElse {
            return "flow-launcher:shizuku:registry-hash-unavailable"
        }
        return if (actualHash == expected.contentHash) {
            null
        } else {
            "flow-launcher:shizuku:registry-hash-mismatch"
        }
    }

    /** @return 失败结局（单原因 + 可选 host 日志原因）。 */
    private fun failed(reason: String, extra: List<String> = emptyList()): ShizukuFlowHostOutcome.Failed =
        ShizukuFlowHostOutcome.Failed(listOf(reason) + extra)

    /** @return 稳定码名（原因用）。 */
    private fun codeName(code: Int): String = when (code) {
        FlowHostProtocol.ERR_ARGS -> "err-args"
        FlowHostProtocol.ERR_SYMBOL -> "err-symbol"
        FlowHostProtocol.ERR_BUSY -> "err-busy"
        FlowHostProtocol.ERR_STATE -> "err-state"
        FlowHostProtocol.ERR_START -> "err-start"
        FlowHostProtocol.ERR_IO -> "err-io"
        else -> "err-unknown-" + code
    }

    private companion object {
        /** 公用 UserService 类全路径（不写 manifest）。 */
        const val SERVICE_CLASS = "com.ghostlock.app.shizuku.GhostlockUserService"

        /** 绑定窗口（计划 B3a：10s）。 */
        const val BIND_TIMEOUT_MS: Long = 10_000L

        /** 绑定等待轮询间隔（G13：可取消；25ms）。 */
        const val BIND_POLL_MS: Long = 25L

        /** 单次 run 上限（B2/B3 fixture：60s）。 */
        const val RUN_TIMEOUT_MS: Long = 60_000L

        /** 状态轮询间隔。 */
        const val POLL_INTERVAL_MS: Long = 100L

        /** G14：连续 state 调用异常阈值（10 × 100ms ≈ 1s ⇒ host-lost）。 */
        const val STATE_FAILURE_LIMIT: Int = 10
    }
}

/** 一次 Shizuku FlowHost run 的中性结局（launcher 负责映射到 UI 状态）。 */
internal sealed interface ShizukuFlowHostOutcome {
    /** host 终态 verify 已解码。 */
    data class Terminal(val verified: FlowHostProtocol.VerifiedTerminal) : ShizukuFlowHostOutcome

    /** 具名失败原因（`flow-launcher:shizuku:*`）。 */
    data class Failed(val reasons: List<String>) : ShizukuFlowHostOutcome
}
