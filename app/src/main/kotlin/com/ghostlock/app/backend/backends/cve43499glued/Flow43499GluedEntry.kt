package com.ghostlock.app.backend.backends.cve43499glued

import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.backend.host.HostProcessExec
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowEntry
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowRequest
import com.ghostlock.app.core.flow.FlowResult
import java.io.File
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions
import kotlin.reflect.KClass

/** 43499-Glued context-free 入口的请求占位（入口只经 [Flow43499GluedEntry.entry] 调用）。 */
class Flow43499GluedEntryRequest : FlowRequest

/**
 * 43499-Glued 的新进程 Flow 入口（B4a 附录 §2；B4b 改用共享 [HostProcessExec]）。
 *
 * 执行链：env.params 解码 profile → 在 `host.run_dir` staging（0600）→
 * `host.native_lib_dir/lib43499-Glued.so --ghostlock-app-call`，stdin=profile.bin，
 * env=`GHOSTLOCK_HOME/TMPDIR=run_dir` + `GHOSTLOCK_KSU_LOG=host.log_path`；
 * stdout/stderr 合并逐行 → `env.log`；有界等待 → 终态 evidence。
 *
 * **context-free**：不读 `env.context`/UI 全局（43284 才需要 Context）。
 * **A11**：超时/取消由 [HostProcessExec] 返回具名结局，**不 kill**载荷（detach）。
 *
 * @note 失败全 `flow-entry:43499:*` / `exec-exit:<n>`；不 fallback 旧 runner。
 * @note 阶段：B4a/B4b；真实 ELF 冷机门禁属 B4b。
 */
object Flow43499GluedEntry : Flow<Flow43499GluedEntryRequest> {
    override val backendClass: KClass<out Flow<*>> = Flow43499GluedEntry::class

    override suspend fun run(request: Flow43499GluedEntryRequest): FlowResult = failed(
        reason = "flow-entry:43499:direct-run-unsupported",
        evidence = emptyMap(),
    )

    override fun stop() = Unit

    /**
     * 执行一次 43499-Glued（host 进程内）。
     *
     * @param env host 运行环境（params 含 profile 三键 + `host.*`；不读 context）。
     * @return Ok（evidence=backend/exit/profile/host_run_dir）或具名 Failed。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        val profile = Glued43499ExecParams.decode(env) ?: return failed(
            reason = "flow-entry:43499:params",
            evidence = emptyMap(),
        )
        val params = env.params
        val runDir = params.getString(FlowEntryParams.KEY_HOST_RUN_DIR)
            ?: return failed(reason = "flow-entry:43499:host-params", evidence = emptyMap())
        val nativeLibDir = params.getString(FlowEntryParams.KEY_HOST_NATIVE_LIB_DIR)
            ?: return failed(reason = "flow-entry:43499:host-params", evidence = emptyMap())
        val logPath = params.getString(FlowEntryParams.KEY_HOST_LOG_PATH)
            ?: return failed(reason = "flow-entry:43499:host-params", evidence = emptyMap())
        val dir = File(runDir)
        if (!dir.isDirectory) {
            return failed(reason = "flow-entry:43499:run-dir-missing", evidence = emptyMap())
        }
        val baseEvidence = linkedMapOf<String, String>(
            "backend" to BACKEND_ID,
            "profile" to profile.release,
            "host_run_dir" to dir.absolutePath,
        )
        val confFile = File(dir, "profile.conf")
        val binFile = File(dir, "profile.bin")
        try {
            writeSecure(confFile, profile.conf.toByteArray(Charsets.UTF_8))
            writeSecure(binFile, profile.bin)
        } catch (problem: Throwable) {
            return failed(
                reason = "flow-entry:43499:stage:" + problem::class.java.simpleName,
                evidence = baseEvidence,
            )
        }
        val binary = File(nativeLibDir, BINARY_NAME)
        if (!binary.isFile) {
            return failed(reason = "flow-entry:43499:binary-missing", evidence = baseEvidence)
        }
        val spec = HostProcessExec.spec(
            binary = binary,
            arguments = listOf(APP_CALL_ARGUMENT),
            directory = dir,
            environment = linkedMapOf(
                ENV_HOME to dir.absolutePath,
                ENV_TMPDIR to dir.absolutePath,
                ENV_KSU_LOG to logPath,
            ),
            stdin = binFile,
            timeoutMs = HostProcessExec.resolveTimeoutMs(params),
        )
        val outcome = HostProcessExec.run(spec, env)
        return when (outcome) {
            is HostProcessExec.Outcome.Completed -> {
                val evidence = baseEvidence + ("exit" to outcome.exitCode.toString())
                if (outcome.exitCode == 0) {
                    FlowResult.Ok(steps = emptyList(), evidence = evidence)
                } else {
                    FlowResult.Failed(
                        reason = "exec-exit:${outcome.exitCode}",
                        failedStep = null,
                        steps = emptyList(),
                        evidence = evidence,
                    )
                }
            }
            is HostProcessExec.Outcome.TimedOut ->
                failed(reason = "flow-entry:43499:timeout", evidence = baseEvidence)

            HostProcessExec.Outcome.Cancelled ->
                failed(reason = "flow-entry:43499:cancelled", evidence = baseEvidence)

            is HostProcessExec.Outcome.StartFailed ->
                failed(
                    reason = "flow-entry:43499:start:" + outcome.reason,
                    evidence = baseEvidence,
                )

            is HostProcessExec.Outcome.StdinFailed ->
                failed(
                    reason = "flow-entry:43499:stdin:" + outcome.reason,
                    evidence = baseEvidence,
                )
        }
    }

    /** @return 写 0600 文件（权限设置 best-effort）。 */
    private fun writeSecure(file: File, bytes: ByteArray) {
        file.writeBytes(bytes)
        runCatching {
            Files.setPosixFilePermissions(
                file.toPath(),
                PosixFilePermissions.fromString("rw-------"),
            )
        }
    }

    /** @return 具名失败（附证据）。 */
    private fun failed(reason: String, evidence: Map<String, String>): FlowResult =
        FlowResult.Failed(
            reason = reason,
            failedStep = null,
            steps = emptyList(),
            evidence = evidence,
        )

    /** 目标 ELF 文件名（旧 executables/新通道一致）。 */
    private const val BINARY_NAME = "lib43499-Glued.so"

    /** upstream CLI 的 app-call 参数。 */
    private const val APP_CALL_ARGUMENT = "--ghostlock-app-call"

    /** host 注入的 HOME 环境变量名。 */
    private const val ENV_HOME = "GHOSTLOCK_HOME"

    /** host 注入的 TMPDIR 环境变量名。 */
    private const val ENV_TMPDIR = "TMPDIR"

    /** host 注入的日志环境变量名。 */
    private const val ENV_KSU_LOG = "GHOSTLOCK_KSU_LOG"

    /** 证据 backend 值。 */
    private const val BACKEND_ID = "43499-Glued"
}
