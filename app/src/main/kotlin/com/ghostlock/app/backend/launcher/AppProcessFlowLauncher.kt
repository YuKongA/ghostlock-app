package com.ghostlock.app.backend.launcher

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.backend.host.FlowHostService
import com.ghostlock.app.core.flow.FlowHostParams
import com.ghostlock.app.core.flow.FlowHostParamsCodec
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import com.ghostlock.app.flow.IFlowHost
import com.ghostlock.app.flow.IFlowHostCallback
import com.ghostlock.app.backend.launcher.BackendExecutionState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import kotlin.coroutines.coroutineContext
import java.util.concurrent.TimeUnit

/**
 * app 进程侧的 `:flowhost` launcher（B2a）。
 *
 * 显式绑定 [FlowHostService]（无隐式解析、无 fallback）；一次 [run] 只绑定/启动一个
 * host 进程，流程：bind → setCallback → prepare → start → 轮询 getState → verify →
 * mapTerminal；finally 关闭回调/销毁/解绑。
 *
 * @param context 任意 context；内部使用 applicationContext。
 * @note 所有失败具名 `flow-launcher:app:*`；不静默回退到旧通道。
 * @note G13：绑定期等待为可取消挂起轮询（取消不再延迟 10s）；超时/失败语义不变。
 * @note 阶段：B2a；设备行为（新 pid/旧进程消失/Bundle wire）由 B2b 门禁。
 */
class AppProcessFlowLauncher(
    context: Context,
    private val expectedRegistryIdentity: BackendRegistryIdentity? = null,
) {
    private val appContext: Context = context.applicationContext

    /** 最近一次 run 的 host 进程 pid（诊断/门禁用；失败为 -1）。 */
    @Volatile
    var lastHostPid: Int = -1
        private set

    /**
     * 执行一次 Flow（挂起直到 host 终态或失败）。
     *
     * @param symbol 反射符号文本（必须命中 host 内置 allowlist）。
     * @param params 参数 map（B1 白名单/预算；local 先校验，避免无谓跨进程）。
     * @param payload 可选 payload（≤512 KiB）。
     * @param onLine 过程日志回调（Binder 线程调用；实现方负责切线程）。
     * @return [BackendExecutionState]（Finished/HandoffRequested/Refused）。
     */
    suspend fun run(
        symbol: String,
        params: Map<String, Any?>,
        payload: ByteArray?,
        onLine: (String) -> Unit,
    ): BackendExecutionState = withContext(Dispatchers.IO) {
        lastHostPid = -1
        val validated = runCatching { FlowHostParams.of(params) }.getOrElse { problem ->
            return@withContext refuse(
                "flow-launcher:app:params:" + (problem.message ?: problem::class.java.simpleName),
            )
        }
        if (payload != null && payload.size > FlowHostProtocol.MAX_PAYLOAD_BYTES) {
            return@withContext refuse("flow-launcher:app:payload-too-large")
        }
        val bundle: Bundle = FlowHostParamsCodec.encode(validated)
        val connection = HostConnection()
        var host: IFlowHost? = null
        var bound = false
        try {
            bound = appContext.bindService(
                Intent(appContext, FlowHostService::class.java),
                connection,
                Context.BIND_AUTO_CREATE,
            )
            if (!bound) return@withContext refuse("flow-launcher:app:bind-failed")
            if (!connection.waitBound(BIND_TIMEOUT_MS)) {
                return@withContext refuse("flow-launcher:app:bind-timeout")
            }
            host = connection.host ?: return@withContext refuse("flow-launcher:app:bind-null")
            registryMismatch(host)?.let { reason ->
                return@withContext refuse(reason)
            }
            host.setCallback(object : IFlowHostCallback.Stub() {
                override fun onLine(line: String?) {
                    runCatching { onLine(line.orEmpty()) }
                }

                override fun onTerminal(code: Int) = Unit
            })
            val prepared = host.prepare(symbol, bundle, payload, null)
            if (prepared != FlowHostProtocol.OK) {
                return@withContext refuse("flow-launcher:app:prepare:" + codeName(prepared))
            }
            lastHostPid = runCatching { host.pid }.getOrDefault(-1)
            val started = host.start()
            if (started != FlowHostProtocol.OK) {
                return@withContext refuse("flow-launcher:app:start:" + codeName(started))
            }
            val terminal = when (val awaited = awaitTerminal(host, connection)) {
                is TerminalWait.Verify -> awaited.bytes
                    ?: return@withContext refuse("flow-launcher:app:timeout")

                TerminalWait.Timeout -> return@withContext refuse("flow-launcher:app:timeout")

                TerminalWait.HostLost -> return@withContext refuse("flow-launcher:app:host-lost")
            }
            val verified = runCatching { FlowHostProtocol.decodeVerify(terminal) }
                .getOrElse { problem ->
                    return@withContext refuse(
                        "flow-launcher:app:verify:" +
                            (problem.message ?: problem::class.java.simpleName),
                    )
                }
            FlowHostProtocol.mapTerminal(verified)
        } catch (cancelled: CancellationException) {
            /* G10：协程取消必须传到 host.cancel（不吞取消）；finally 清理顺序不变。 */
            runCatching { host?.cancel() }
            throw cancelled
        } finally {
            runCatching { host?.clearCallback() }
            runCatching { host?.cancel() }
            runCatching { host?.destroy() }
            if (bound) runCatching { appContext.unbindService(connection) }
        }
    }

    /**
     * 等待终态（G14：区分正常超时与 host 丢失）。
     */
    private sealed interface TerminalWait {
        /** host 已 TERMINAL；verify 读取失败时 bytes=null（调用方按 timeout 处理）。 */
        data class Verify(val bytes: ByteArray?) : TerminalWait

        /** 60s 总超时（行为同旧实现）。 */
        data object Timeout : TerminalWait

        /** host 进程/binder 丢失（≤2s 快速失败，具名 host-lost）。 */
        data object HostLost : TerminalWait
    }

    /**
     * 轮询 host 状态直到 TERMINAL。
     *
     * @param host 已启动的 host。
     * @param connection 绑定回调（提供断开标志）。
     * @return [TerminalWait]；connected 丢失或连续 state 异常 ⇒ HostLost。
     */
    private suspend fun awaitTerminal(
        host: IFlowHost,
        connection: HostConnection,
    ): TerminalWait {
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(RUN_TIMEOUT_MS)
        var consecutiveStateFailures = 0
        while (System.nanoTime() < deadline) {
            /* G10：可取消轮询（不再用阻塞 sleep 独占取消传播）。 */
            coroutineContext.ensureActive()
            /* G14：host 丢失快速失败（≤2s；binder death 回调或连续调用异常）。 */
            if (connection.disconnected || connection.host == null) {
                return TerminalWait.HostLost
            }
            val stateResult = runCatching { host.state }
            if (stateResult.isFailure) {
                consecutiveStateFailures += 1
                if (consecutiveStateFailures >= STATE_FAILURE_LIMIT) {
                    return TerminalWait.HostLost
                }
            } else {
                consecutiveStateFailures = 0
                when (val current = stateResult.getOrNull()) {
                    FlowHostProtocol.TERMINAL ->
                        return TerminalWait.Verify(runCatching { host.verify() }.getOrNull())

                    FlowHostProtocol.IDLE, FlowHostProtocol.PREPARED -> return TerminalWait.Timeout

                    else -> Unit
                }
            }
            delay(POLL_INTERVAL_MS)
        }
        return TerminalWait.Timeout
    }

    /** @return Refused 状态（单原因）。 */
    private fun refuse(reason: String): BackendExecutionState =
        BackendExecutionState.Refused(listOf(reason))

    /**
     * Compares the app-root registry identity with the newly bound host before prepare.
     *
     * @param host Bound one-shot host.
     * @return Null when comparison is disabled or exact; otherwise a stable refusal reason.
     */
    private fun registryMismatch(host: IFlowHost): String? {
        val expected = expectedRegistryIdentity ?: return null
        val actualVersion = runCatching { host.registryVersion }.getOrElse {
            return "flow-launcher:app:registry-version-unavailable"
        }
        if (actualVersion != expected.version) {
            return "flow-launcher:app:registry-version-mismatch"
        }
        val actualHash = runCatching { host.registryContentHash }.getOrElse {
            return "flow-launcher:app:registry-hash-unavailable"
        }
        return if (actualHash == expected.contentHash) {
            null
        } else {
            "flow-launcher:app:registry-hash-mismatch"
        }
    }

    /** @return 稳定码名（日志/原因用）。 */
    private fun codeName(code: Int): String = when (code) {
        FlowHostProtocol.ERR_ARGS -> "err-args"
        FlowHostProtocol.ERR_SYMBOL -> "err-symbol"
        FlowHostProtocol.ERR_BUSY -> "err-busy"
        FlowHostProtocol.ERR_STATE -> "err-state"
        FlowHostProtocol.ERR_START -> "err-start"
        FlowHostProtocol.ERR_IO -> "err-io"
        else -> "err-unknown-" + code
    }

    /** 绑定回调：保存 host 接口；绑定等待是**可取消**挂起轮询（G13/G14）。 */
    private class HostConnection : ServiceConnection {
        /** @return 已连接 host；未连接为 null。 */
        @Volatile
        var host: IFlowHost? = null

        /** G14：host 进程/binder 丢失标志（onServiceDisconnected/onBindingDied/onNullBinding）。 */
        @Volatile
        var disconnected: Boolean = false

        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            disconnected = false
            host = IFlowHost.Stub.asInterface(service)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            disconnected = true
            host = null
        }

        override fun onBindingDied(name: ComponentName?) {
            disconnected = true
            host = null
        }

        override fun onNullBinding(name: ComponentName?) {
            disconnected = true
            host = null
        }

        /**
         * 可取消绑定等待（25ms 轮询 + ensureActive）。
         *
         * @param timeoutMs 绑定上限（ms）。
         * @return true=超时前已连接。
         * @throws kotlinx.coroutines.CancellationException 协程取消时立即抛出。
         */
        suspend fun waitBound(timeoutMs: Long): Boolean {
            val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs)
            while (System.nanoTime() < deadline) {
                coroutineContext.ensureActive()
                if (host != null) return true
                delay(BIND_POLL_MS)
            }
            return host != null
        }
    }

    private companion object {
        /** 绑定超时（计划 B2a：10s）。 */
        const val BIND_TIMEOUT_MS: Long = 10_000L

        /** 绑定等待轮询间隔（G13：可取消；25ms）。 */
        const val BIND_POLL_MS: Long = 25L

        /** B2 fixture 的运行上限（计划 B2a：60s）。 */
        const val RUN_TIMEOUT_MS: Long = 60_000L

        /** 状态轮询间隔。 */
        const val POLL_INTERVAL_MS: Long = 100L

        /** G14：连续 state 调用异常阈值（10 × 100ms ≈ 1s ⇒ host-lost）。 */
        const val STATE_FAILURE_LIMIT: Int = 10
    }
}
