package com.ghostlock.app.backend.backends.cve43284glued

import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.backend.host.HostProcessExec
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowEntry
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowRequest
import com.ghostlock.app.core.flow.FlowResult
import java.io.File
import kotlin.reflect.KClass

/** 43284-Glued context-aware 入口的请求占位（入口只经 [Flow43284GluedEntry.entry] 调用）。 */
class Flow43284GluedEntryRequest : FlowRequest

/**
 * 43284-Glued 的新进程 Flow 入口（B4b 附录 §2 + 复核 B2 生命周期）。
 *
 * 执行链：`env.context` 必需 → [AndroidIpSecRuntime.create] → `host.native_lib_dir/lib43284Glued.so
 * + runtime.arguments`（directory=host.run_dir、无 stdin、merged stderr）→ 逐行 `env.log` →
 * 有界等待/取消（超时/取消**不 kill**，A11）→ evidence。
 *
 * **runtime 生命周期（B2）**：
 * - ELF 已退出（正常/启动失败/stdin 失败）→ `runtime.close()`；成功 `ipsec=runtime-closed`，
 *   失败 ⇒ `flow-entry:43284:ipsec-close:<detail>` + `ipsec=runtime-close-failed`；
 * - 超时/取消（ELF 可能仍存活）⇒ **不 close**：`ipsec=runtime-leaked` +
 *   `host_keepalive=true`，返回具名 Failed；由 host 保活进程持有 runtime。
 *
 * @note 失败全 `flow-entry:43284:*` / `exec-exit:<n>`；`context==null` ⇒
 *   `flow-host:context-required`（不 fallback 旧 runner/app flavor）。
 * @note Context 仅 host 进程本地使用；入口不 close/invalidate host Context。
 * @note 阶段：B4b；真实攻击冷机门禁属 B5。
 */
object Flow43284GluedEntry : Flow<Flow43284GluedEntryRequest> {
    override val backendClass: KClass<out Flow<*>> = Flow43284GluedEntry::class

    override suspend fun run(request: Flow43284GluedEntryRequest): FlowResult = failed(
        reason = "flow-entry:43284:direct-run-unsupported",
        evidence = emptyMap(),
    )

    override fun stop() = Unit

    /**
     * 执行一次 43284-Glued（host 进程内；需要 host 提供的 Context）。
     *
     * @param env host 运行环境（context + `host.*` 参数）。
     * @return Ok（evidence=backend/exit/host_run_dir/ipsec）或具名 Failed。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        val context = env.context
            ?: return failed(reason = "flow-host:context-required", evidence = emptyMap())
        val params = env.params
        val runDir = params.getString(FlowEntryParams.KEY_HOST_RUN_DIR)
            ?: return failed(reason = "flow-entry:43284:host-params", evidence = emptyMap())
        val nativeLibDir = params.getString(FlowEntryParams.KEY_HOST_NATIVE_LIB_DIR)
            ?: return failed(reason = "flow-entry:43284:host-params", evidence = emptyMap())
        val logPath = params.getString(FlowEntryParams.KEY_HOST_LOG_PATH)
            ?: return failed(reason = "flow-entry:43284:host-params", evidence = emptyMap())
        val dir = File(runDir)
        if (!dir.isDirectory) {
            return failed(reason = "flow-entry:43284:run-dir-missing", evidence = emptyMap())
        }
        val binary = File(nativeLibDir, BINARY_NAME)
        if (!binary.isFile) {
            return failed(reason = "flow-entry:43284:binary-missing", evidence = emptyMap())
        }
        val baseEvidence = linkedMapOf<String, String>(
            "backend" to BACKEND_ID,
            "host_run_dir" to dir.absolutePath,
        )
        val runtime = try {
            AndroidIpSecRuntime.create(context)
        } catch (problem: Throwable) {
            return failed(
                reason = "flow-entry:43284:ipsec:" + problem::class.java.simpleName,
                evidence = baseEvidence,
            )
        }
        val spec = HostProcessExec.spec(
            binary = binary,
            arguments = runtime.arguments,
            directory = dir,
            environment = linkedMapOf(
                ENV_HOME to dir.absolutePath,
                ENV_TMPDIR to dir.absolutePath,
                ENV_KSU_LOG to logPath,
            ),
            stdin = null,
            timeoutMs = HostProcessExec.resolveTimeoutMs(params),
        )
        return when (val outcome = HostProcessExec.run(spec, env)) {
            is HostProcessExec.Outcome.Completed ->
                completed(runtime, baseEvidence, outcome.exitCode)

            is HostProcessExec.Outcome.TimedOut -> leaked(
                reason = "flow-entry:43284:timeout",
                evidence = baseEvidence,
            )

            HostProcessExec.Outcome.Cancelled -> leaked(
                reason = "flow-entry:43284:cancelled",
                evidence = baseEvidence,
            )

            is HostProcessExec.Outcome.StartFailed -> closeAndFail(
                runtime = runtime,
                reason = "flow-entry:43284:start:" + outcome.reason,
                evidence = baseEvidence,
            )

            is HostProcessExec.Outcome.StdinFailed -> closeAndFail(
                runtime = runtime,
                reason = "flow-entry:43284:stdin:" + outcome.reason,
                evidence = baseEvidence,
            )
        }
    }

    /**
     * 正常退出路径：先 close runtime，再按 exit code 产出 Ok/Failed。
     *
     * @param runtime 已创建的 IPsec runtime。
     * @param baseEvidence backend/host_run_dir。
     * @param exitCode ELF 退出码。
     * @return Ok（exit=0）或 Failed（exec-exit/close 失败）。
     */
    private fun completed(
        runtime: AndroidIpSecRuntime,
        baseEvidence: Map<String, String>,
        exitCode: Int,
    ): FlowResult {
        val closeProblem = runCatching { runtime.close() }.exceptionOrNull()
        if (closeProblem != null) {
            return failed(
                reason = "flow-entry:43284:ipsec-close:" +
                    (closeProblem.message ?: closeProblem::class.java.simpleName),
                evidence = baseEvidence + linkedMapOf(
                    "exit" to exitCode.toString(),
                    "ipsec" to "runtime-close-failed",
                ),
            )
        }
        val evidence = baseEvidence + linkedMapOf(
            "exit" to exitCode.toString(),
            "ipsec" to "runtime-closed",
        )
        return if (exitCode == 0) {
            FlowResult.Ok(steps = emptyList(), evidence = evidence)
        } else {
            FlowResult.Failed(
                reason = "exec-exit:$exitCode",
                failedStep = null,
                steps = emptyList(),
                evidence = evidence,
            )
        }
    }

    /**
     * 已退出但异常路径：close runtime 后返回具名失败（无 ELF 存活，不触发保活）。
     *
     * @param runtime 已创建 runtime。
     * @param reason 具名失败原因。
     * @param evidence base evidence。
     * @return Failed（ipsec=runtime-closed|runtime-close-failed）。
     */
    private fun closeAndFail(
        runtime: AndroidIpSecRuntime,
        reason: String,
        evidence: Map<String, String>,
    ): FlowResult {
        val closeProblem = runCatching { runtime.close() }.exceptionOrNull()
        val ipsec = if (closeProblem == null) "runtime-closed" else "runtime-close-failed"
        return failed(reason = reason, evidence = evidence + ("ipsec" to ipsec))
    }

    /**
     * 超时/取消：ELF 可能仍存活 ⇒ 不 close runtime；host 必须保活。
     *
     * @param reason 具名失败原因。
     * @param evidence base evidence。
     * @return Failed（ipsec=runtime-leaked + host_keepalive=true）。
     */
    private fun leaked(reason: String, evidence: Map<String, String>): FlowResult = failed(
        reason = reason,
        evidence = evidence + linkedMapOf(
            "ipsec" to "runtime-leaked",
            HostProcessExec.EVIDENCE_HOST_KEEPALIVE to HostProcessExec.EVIDENCE_KEEPALIVE_TRUE,
        ),
    )

    /** @return 具名失败（附证据）。 */
    private fun failed(reason: String, evidence: Map<String, String>): FlowResult =
        FlowResult.Failed(
            reason = reason,
            failedStep = null,
            steps = emptyList(),
            evidence = evidence,
        )

    /** 目标 ELF 文件名（旧 executables/新通道一致）。 */
    private const val BINARY_NAME = "lib43284Glued.so"

    /** host 注入的 HOME 环境变量名。 */
    private const val ENV_HOME = "GHOSTLOCK_HOME"

    /** host 注入的 TMPDIR 环境变量名。 */
    private const val ENV_TMPDIR = "TMPDIR"

    /** host 注入的日志环境变量名。 */
    private const val ENV_KSU_LOG = "GHOSTLOCK_KSU_LOG"

    /** 证据 backend 值。 */
    private const val BACKEND_ID = "43284Glued"
}
