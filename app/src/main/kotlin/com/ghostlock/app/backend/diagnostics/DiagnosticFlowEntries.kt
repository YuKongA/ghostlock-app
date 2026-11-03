package com.ghostlock.app.backend.diagnostics

import com.ghostlock.app.backend.host.FlowHostProtocol
import com.ghostlock.app.backend.host.HostProcessExec
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowEntry
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowRequest
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.FlowSymbol
import kotlin.reflect.KClass

/**
 * B2a 诊断 fixture 的请求类型（无字段；不进入生产注册表）。
 *
 * @note internal（模块内可见）；仅用于无副作用 smoke 测试；不得被 UI/BackendSpec 引用。
 */
internal class DiagnosticRequest : FlowRequest

/**
 * 无副作用 fixture：写 3 行 diag 日志并返回 Ok（不触任何攻击/文件/进程）。
 *
 * @note internal：不可被 UI 选择、不得注册为 BackendSpec；仅 `:flowhost` allowlist 引用。
 * @note 阶段：B2a smoke（B2b 设备门禁用）。
 */
internal object DiagnosticOkFlow : Flow<DiagnosticRequest> {
    override val backendClass: KClass<out Flow<*>> = DiagnosticOkFlow::class

    override suspend fun run(request: DiagnosticRequest): FlowResult = ok()

    override fun stop() = Unit

    /**
     * @param env host 运行环境（只用于写日志）。
     * @return Ok（evidence=diag=ok）。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        env.log("diag:line:1")
        env.log("diag:line:2")
        env.log("diag:line:3")
        return ok()
    }

    /** @return 固定 Ok 结果。 */
    private fun ok(): FlowResult =
        FlowResult.Ok(emptyList(), mapOf("diag" to "ok"))
}

/**
 * 无副作用 fixture：返回 Ok + handoff 证据（服务把它映射为 handoff 终态）。
 *
 * @note 不得执行任何攻击/文件写；handoff 语义由 [FlowHostProtocol.HANDOFF_EVIDENCE_KEY] 承载。
 * @note 阶段：B2a smoke。
 */
internal object DiagnosticHandoffFlow : Flow<DiagnosticRequest> {
    override val backendClass: KClass<out Flow<*>> = DiagnosticHandoffFlow::class

    override suspend fun run(request: DiagnosticRequest): FlowResult = ok()

    override fun stop() = Unit

    /**
     * @param env host 运行环境（写 1 行日志）。
     * @return Ok（evidence.handoff=requested）。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        env.log("diag:handoff:requested")
        return ok()
    }

    /** @return 带 handoff 证据的 Ok 结果。 */
    private fun ok(): FlowResult = FlowResult.Ok(
        emptyList(),
        mapOf(FlowHostProtocol.HANDOFF_EVIDENCE_KEY to FlowHostProtocol.HANDOFF_EVIDENCE_VALUE),
    )
}

/**
 * 无副作用慢 fixture（G10 取消门禁）：循环约 20s（100ms tick），观察 [FlowHostEnv.isCancelled]。
 *
 * @note 不触攻击/文件/进程；取消时返回 `diag:cancelled`（A11：不 kill 载荷由 host 处置）。
 * @note 阶段：G10（设备取消门禁）。
 */
internal object DiagnosticSlowFlow : Flow<DiagnosticRequest> {
    override val backendClass: KClass<out Flow<*>> = DiagnosticSlowFlow::class

    override suspend fun run(request: DiagnosticRequest): FlowResult = ok()

    override fun stop() = Unit

    /**
     * @param env host 运行环境（日志 + 取消标志）。
     * @return 取消 ⇒ Failed(`diag:cancelled`)；正常 ⇒ Ok(`diag=slow-done`)。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        for (tick in 0 until SLOW_TICKS) {
            if (env.isCancelled()) {
                return FlowResult.Failed(
                    reason = CANCELLED_REASON,
                    failedStep = null,
                    steps = emptyList(),
                    evidence = emptyMap(),
                )
            }
            env.log("diag:slow:tick:$tick")
            try {
                Thread.sleep(SLOW_TICK_MS)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
                return FlowResult.Failed(
                    reason = CANCELLED_REASON,
                    failedStep = null,
                    steps = emptyList(),
                    evidence = emptyMap(),
                )
            }
        }
        return ok()
    }

    /** @return 正常完成结果。 */
    private fun ok(): FlowResult = FlowResult.Ok(
        emptyList(),
        mapOf("diag" to "slow-done"),
    )

    /** 循环 tick 数（200 × 100ms ≈ 20s）。 */
    private const val SLOW_TICKS = 200

    /** 每 tick 间隔（ms）。 */
    private const val SLOW_TICK_MS = 100L

    /** 取消具名原因。 */
    private const val CANCELLED_REASON = "diag:cancelled"
}

/**
 * 无副作用 keepalive fixture（G11 A11 门禁）：返回 `host_keepalive=true` 触发 host 保活路径。
 *
 * @note 不触攻击/文件/进程；host 终端据 evidence 跳过清理/自杀（A11 资源保全）。
 * @note 阶段：G11（设备 keepalive 门禁）。
 */
internal object DiagnosticKeepaliveFlow : Flow<DiagnosticRequest> {
    override val backendClass: KClass<out Flow<*>> = DiagnosticKeepaliveFlow::class

    override suspend fun run(request: DiagnosticRequest): FlowResult = failed()

    override fun stop() = Unit

    /**
     * @param env host 运行环境（只写 1 行日志）。
     * @return Failed(`diag:keepalive`) + evidence `host_keepalive=true`。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        env.log("diag:keepalive:requested")
        return failed()
    }

    /** @return 具名失败 + keepalive evidence。 */
    private fun failed(): FlowResult = FlowResult.Failed(
        reason = "diag:keepalive",
        failedStep = null,
        steps = emptyList(),
        evidence = mapOf(
            HostProcessExec.EVIDENCE_HOST_KEEPALIVE to HostProcessExec.EVIDENCE_KEEPALIVE_TRUE,
        ),
    )
}

/**
 * 诊断 fixture 的符号集合（G9：仅 debug host allowlist 通过
 * [com.ghostlock.app.backend.declarations.FlowEntryRegistryAssembly.hostRegistry] 追加）。
 *
 * @note internal；不得注册为 BackendSpec/进入 release 生产 allowlist。
 */
internal object DiagnosticFlowEntries {
    /** @return 两个无副作用 fixture 的 `@FlowEntry` 符号（顺序稳定）。 */
    val symbols: List<FlowSymbol> = listOf(
        FlowSymbol(DiagnosticOkFlow::class.java.name, "entry"),
        FlowSymbol(DiagnosticHandoffFlow::class.java.name, "entry"),
        FlowSymbol(DiagnosticSlowFlow::class.java.name, "entry"),
        FlowSymbol(DiagnosticKeepaliveFlow::class.java.name, "entry"),
    )
}
