package com.ghostlock.app.backend.launcher

import android.content.Context
import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.core.flow.FlowHostParams
import com.ghostlock.app.core.flow.FlowHostParamsCodec
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import com.ghostlock.app.shizuku.ShizukuFlowHostClient
import com.ghostlock.app.shizuku.ShizukuFlowHostOutcome
import com.ghostlock.app.backend.launcher.BackendExecutionState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/**
 * Shizuku 进程侧的通用 Flow launcher（B3a）。
 *
 * 对外 API 与 [AppProcessFlowLauncher] 同形；内部委托 [ShizukuFlowHostClient] 绑定
 * `GhostlockUserService`，把 host verify 终态经 [FlowHostProtocol.mapTerminal] 映射为 UI 状态。
 *
 * @param context 任意 context；内部使用 applicationContext。
 * @note 失败全部具名 `flow-launcher:shizuku:*`；不 fallback 到 app 通道。
 * @note 阶段：B3a；设备 B3b 门禁（A301SO 无 Shizuku 包）。
 */
class ShizukuProcessFlowLauncher(
    context: Context,
    private val expectedRegistryIdentity: BackendRegistryIdentity? = null,
) {
    private val appContext: Context = context.applicationContext

    /** 最近一次 run 的 shell 进程 pid（诊断/门禁用；失败 -1）。 */
    @Volatile
    var lastHostPid: Int = -1
        private set

    /**
     * 执行一次 Shizuku-host Flow（挂起直到终态或失败）。
     *
     * @param symbol 反射符号（只命中 host 内置诊断 allowlist）。
     * @param params 参数 map（B1 白名单/预算；本地先校验）。
     * @param payload 可选 payload（≤512 KiB）。
     * @param onLine 过程日志回调（Binder 线程）。
     * @return [BackendExecutionState]（Finished/HandoffRequested/Refused）。
     * @throws kotlinx.coroutines.CancellationException 协程取消：先 cancelFlow + 回写 [lastHostPid] 后原样抛出。
     */
    suspend fun run(
        symbol: String,
        params: Map<String, Any?>,
        payload: ByteArray?,
        onLine: (String) -> Unit,
    ): BackendExecutionState = withContext(Dispatchers.IO) {
        lastHostPid = -1
        coroutineContext.ensureActive()
        val validated = runCatching { FlowHostParams.of(params) }.getOrElse { problem ->
            return@withContext refuse(
                "flow-launcher:shizuku:params:" +
                    (problem.message ?: problem::class.java.simpleName),
            )
        }
        if (payload != null && payload.size > FlowHostProtocol.MAX_PAYLOAD_BYTES) {
            return@withContext refuse("flow-launcher:shizuku:payload-too-large")
        }
        val bundle = FlowHostParamsCodec.encode(validated)
        val client = ShizukuFlowHostClient(appContext, expectedRegistryIdentity)
        val outcome = try {
            client.execute(symbol, bundle, payload, onLine)
        } catch (cancelled: CancellationException) {
            /* G10c：client 已在取消路径 cancelFlow；先回写 shell pid 供取消门禁断言，再 rethrow。 */
            lastHostPid = client.lastHostPid
            throw cancelled
        }
        when (outcome) {
            is ShizukuFlowHostOutcome.Failed ->
                BackendExecutionState.Refused(outcome.reasons)
            is ShizukuFlowHostOutcome.Terminal -> {
                lastHostPid = client.lastHostPid
                FlowHostProtocol.mapTerminal(outcome.verified)
            }
        }
    }

    /** @return Refused 状态（单原因）。 */
    private fun refuse(reason: String): BackendExecutionState =
        BackendExecutionState.Refused(listOf(reason))
}
