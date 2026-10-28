package com.ghostlock.app.backend.cve98260.flow

import com.ghostlock.app.backend.cve98260.finish.Cve98260FinishAcceptance
import com.ghostlock.app.backend.cve98260.finish.Cve98260FinishEvidence
import com.ghostlock.app.backend.cve98260.markers.Cve98260Markers
import com.ghostlock.app.backend.cve98260.run.Cve98260ServicePort
import com.ghostlock.app.core.flow.FlowStep
import com.ghostlock.app.core.flow.SequentialFlow
import com.ghostlock.app.core.flow.StepResult
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import java.security.MessageDigest
import kotlin.math.min

/**
 * CVE-2026-98260 的唯一 app 侧编排（B2；ADR-0022）。
 *
 * 步骤固定：`validate → prepare → start → poll → verify → acceptance`。
 * retry/timeout/cancel/one-shot/cleanup 语义全部继承 [SequentialFlow]；service 只回执原语。
 * 终态规则（delta §3/§E）：
 * - 未 armed 的失败 ⇒ Failed（mustReboot=false，`cve98260:<stage>:<reason>`）；
 * - armed 后 park/超时/异常 ⇒ Failed + `mustReboot=true`，**不 kill**，先落 store 再返回；
 * - 成功（KSU 判据 + verify）⇒ **先落 store `must_reboot=true`** 再返回 Ok（PARKED 语义）。
 *
 * @note 真机门禁 G8–G13 未过；本类不直接依赖 Android API。
 */
internal class Flow98260Impl : SequentialFlow<Flow98260Request>(Flow98260Impl::class) {

    /** 本次运行的内部状态（steps 内创建、cleanup 兜底读取）。 */
    private var active: RunState? = null

    override fun steps(request: Flow98260Request): List<FlowStep> {
        val run = RunState(request)
        active = run
        request.progress.onLine("cve98260 runId=" + request.runId)
        return listOf(
            FlowStep(STEP_VALIDATE) { validate(run) },
            FlowStep(STEP_PREPARE) { prepare(run) },
            FlowStep(STEP_START) { start(run) },
            FlowStep(STEP_POLL) { poll(run) },
            FlowStep(STEP_VERIFY) { verify(run) },
            FlowStep(STEP_ACCEPTANCE) { acceptance(run) },
        )
    }

    override suspend fun cleanup() {
        val run = active ?: return
        active = null
        /* cleanup 在 NonCancellable 中恰好一次；armed/不确定时只解绑、不触发 destroy。 */
        runCatching { run.request.port.disconnect(remove = run.terminalSafe) }
        runCatching { run.request.progress.onLine("cve98260 cleanup remove=" + run.terminalSafe) }
    }

    /** step：app 初筛（F9/F10/F12 + release）。 */
    private suspend fun validate(run: RunState): StepResult {
        val request = run.request
        val release = request.safety.kernelRelease()
        if (!request.profile.matchesDevice(release)) {
            return StepResult.Failed(
                "cve98260:validate:release-mismatch:" + (release ?: "unknown"),
                mapOf("expectedRelease" to request.profile.release),
            )
        }
        val bootId = request.safety.bootId()
        run.bootId = bootId
        if (bootId.isNullOrBlank()) {
            return StepResult.Failed("cve98260:validate:boot-id-unknown")
        }
        if (request.safety.storeBlocked()) {
            return StepResult.Failed("cve98260:validate:must-reboot")
        }
        return StepResult.Done(
            mapOf(
                "bootId" to bootId,
                "release" to (release ?: ""),
                "payloadSha256" to request.payloadSha256,
            ),
        )
    }

    /** step：launch_pending 先落盘 → AIDL prepare（service 预检/写 payload/journal）。 */
    private suspend fun prepare(run: RunState): StepResult {
        val request = run.request
        if (!request.safety.markLaunchPending(run.bootId)) {
            return StepResult.Failed(
                "cve98260:prepare:persist-failed",
                mapOf("mustReboot" to "true", "outcome" to "persist-failed"),
            )
        }
        val code = request.port.prepare(
            request.profile.toParamArg(),
            request.payload,
            request.finishScript,
            request.runId,
            request.helperPath,
        ) { line -> request.progress.onLine(line) }
        if (code != Cve98260ServicePort.Code.OK) {
            val reason = request.port.lastReason()
            persistNotArmed(run)
            return StepResult.Failed(
                "cve98260:prepare:" + code + (if (reason.isBlank()) "" else ":" + reason),
                mapOf("mustReboot" to "false"),
            )
        }
        run.prepared = true
        return StepResult.Done(mapOf("prepared" to "true"))
    }

    /** step：AIDL start（journal launched → 公用 victim launcher → pid 存活）。 */
    private suspend fun start(run: RunState): StepResult {
        val result = run.request.port.start()
        if (result <= 0) {
            val reason = run.request.port.lastReason()
            persistNotArmed(run)
            return StepResult.Failed(
                "cve98260:start:" + result + (if (reason.isBlank()) "" else ":" + reason),
                mapOf("mustReboot" to "false"),
            )
        }
        run.pid = result
        run.request.progress.onLine("cve98260 payload pid=" + result)
        return StepResult.Done(mapOf("pid" to result.toString()))
    }

    /** step：进程内（app）轮询日志/marker；到期=indeterminate（不发信号）。 */
    private suspend fun poll(run: RunState): StepResult {
        val request = run.request
        val deadline = System.currentTimeMillis() + request.pollWindowMs
        while (true) {
            currentCoroutineContext().ensureActive()
            val chunk = request.port.readLog(run.offset, request.maxLogBytes)
            if (chunk.isNotEmpty()) {
                run.offset += chunk.size.toLong()
                val text = run.carry + String(chunk, Charsets.UTF_8)
                val lines = text.split('\n')
                run.carry = lines.last()
                val complete = lines.subList(0, lines.size - 1)
                run.markers = Cve98260Markers.parse(complete, run.markers)
                for (line in complete) request.progress.onLine(line)
            }
            if (run.markers.parkedReason != null) {
                return indeterminate(run, "cve98260:park-unreaped")
            }
            if (run.markers.preflightFail != null) {
                return StepResult.Failed(
                    "cve98260:preflight:" + run.markers.preflightFail,
                    mapOf("mustReboot" to if (run.markers.armed) "true" else "false"),
                )
            }
            if (run.markers.helperSucceeded()) {
                run.helperSucceeded = true
                return StepResult.Done(
                    mapOf(
                        "markers" to "KSU_LOADED",
                        "pid" to run.pid.toString(),
                        "armed" to run.markers.armed.toString(),
                    ),
                )
            }
            if (System.currentTimeMillis() >= deadline) {
                return indeterminate(run, "cve98260:poll-window-timeout")
            }
            delay(request.pollIntervalMs)
        }
    }

    /** step：独立验证（service 读 /proc/modules + finish 证据）；不把日志当唯一判据。 */
    private suspend fun verify(run: RunState): StepResult {
        val evidenceBytes = run.request.port.verify()
        val lines = String(evidenceBytes, Charsets.UTF_8).split('\n')
            .filter { it.isNotBlank() }
        run.verifyEvidence = lines.joinToString(",")
        run.verifyFacts = lines.mapNotNull { line ->
            val index = line.indexOf('=')
            if (index <= 0) null else line.substring(0, index) to line.substring(index + 1)
        }.toMap()
        if (!run.helperSucceeded) {
            return StepResult.Failed("cve98260:verify:no-helper-success", mapOf("mustReboot" to "true"))
        }
        val ksuLoaded = run.verifyFacts["ksu_loaded"] == "1"
        if (!ksuLoaded) {
            return indeterminate(run, "cve98260:verify:ksu-not-loaded")
        }
        return StepResult.Done(
            mapOf(
                "ksuLoaded" to "true",
                "payloadPid" to (run.verifyFacts["payload_pid"] ?: ""),
                "verifyEvidence" to run.verifyEvidence,
            ),
        )
    }

    /**
     * step：finish 证据验收（独立 `Cve98260FinishAcceptance`），成功也落 `must_reboot=true`。
     *
     * @note 证据全部来自 service `verify()`（app 只装配/对拍 sha）；验收失败 ⇒ Failed +
     *   先落 store must_reboot=true（armed 后只能重启清理）。
     */
    private fun acceptance(run: RunState): StepResult {
        val request = run.request
        val facts = run.verifyFacts
        val evidence = Cve98260FinishEvidence(
            runId = request.runId,
            scriptSha256 = facts["finish_script_sha256"] ?: "",
            expectedScriptSha256 = sha256Hex(request.finishScript),
            scriptPresent = facts["finish_script_present"] == "1",
            exitCode = facts["finish_exit"]?.toIntOrNull(),
            timedOut = facts["finish_timeout"] == "1",
            observedUid = facts["finish_uid"]?.toLongOrNull(),
            selinuxEnforcing = when (facts["finish_enforce"]) {
                "1" -> true
                null, "unknown" -> null
                else -> false
            },
            ksudPresent = when (facts["finish_ksud"]) {
                "1" -> true
                "0" -> false
                else -> null
            },
            requireSelinuxEnforcing = request.finishRequiresSelinux,
        )
        when (val verdict = Cve98260FinishAcceptance.evaluate(evidence)) {
            is Cve98260FinishAcceptance.Verdict.Refused -> {
                val reason = verdict.reasons.firstOrNull() ?: "cve98260-finish:refused"
                val persisted = request.safety.markTerminal(
                    bootId = run.bootId,
                    mustReboot = true,
                    outcome = "failed",
                    reason = reason,
                )
                return StepResult.Failed(
                    "cve98260:finish:" + reason,
                    mapOf("mustReboot" to "true", "persisted" to persisted.toString()),
                )
            }
            is Cve98260FinishAcceptance.Verdict.Accepted -> Unit
        }
        val persisted = request.safety.markTerminal(
            bootId = run.bootId,
            mustReboot = true,
            outcome = "ok",
            reason = "cve98260:ok",
        )
        if (!persisted) {
            return StepResult.Failed(
                "cve98260:acceptance:persist-failed",
                mapOf("mustReboot" to "true", "outcome" to "persist-failed"),
            )
        }
        run.terminalSafe = true
        return StepResult.Done(
            mapOf(
                "runId" to request.runId,
                "mustReboot" to "true",
                "outcome" to "ok",
                "scriptSha256" to evidence.scriptSha256,
                "verifyEvidence" to run.verifyEvidence,
            ),
        )
    }

    /** @return 小写 hex sha256；算法失败空串（acceptance 会按 mismatch 拒绝）。 */
    private fun sha256Hex(bytes: ByteArray): String = runCatching {
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }
    }.getOrDefault("")

    /**
     * armed 后不确定：先落 store（must_reboot=true），再返回 Failed（不 kill）。
     *
     * @note `SequentialFlow` 不把 Failed step 的 evidence 合并进 `FlowResult.Failed.evidence`；
     *   因此 UI 的重启警示必须以 `BootSafetyStore` 为唯一权威（本函数已先落盘）。
     */
    private fun indeterminate(run: RunState, reason: String): StepResult {
        run.request.safety.markTerminal(
            bootId = run.bootId,
            mustReboot = true,
            outcome = "indeterminate",
            reason = reason,
        )
        return StepResult.Failed(reason, mapOf("mustReboot" to "true", "pid" to run.pid.toString()))
    }

    /** 未 armed 的失败：落普通终态（不置 must_reboot）；持久化失败升级为 persist-failed。 */
    private fun persistNotArmed(run: RunState) {
        val persisted = run.request.safety.markTerminal(
            bootId = run.bootId,
            mustReboot = false,
            outcome = "failed",
            reason = null,
        )
        run.terminalSafe = true
        if (!persisted) {
            run.request.progress.onLine("cve98260 persist-failed (not armed)")
        }
    }

    /** 一次运行的可变状态（只在 Flow 实例内）。 */
    private class RunState(val request: Flow98260Request) {
        var bootId: String? = null
        var prepared: Boolean = false
        var pid: Int = 0
        var offset: Long = 0L
        var carry: String = ""
        var markers: Cve98260Markers.Snapshot = Cve98260Markers.Snapshot()
        var helperSucceeded: Boolean = false
        var verifyEvidence: String = ""
        var verifyFacts: Map<String, String> = emptyMap()
        var terminalSafe: Boolean = false
    }

    private companion object {
        private const val STEP_VALIDATE = "cve98260.validate"
        private const val STEP_PREPARE = "cve98260.prepare"
        private const val STEP_START = "cve98260.start"
        private const val STEP_POLL = "cve98260.poll"
        private const val STEP_VERIFY = "cve98260.verify"
        private const val STEP_ACCEPTANCE = "cve98260.acceptance"
    }
}
