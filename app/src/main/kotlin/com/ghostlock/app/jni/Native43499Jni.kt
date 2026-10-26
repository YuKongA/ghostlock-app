package com.ghostlock.app.jni

import androidx.annotation.Keep

/** Placement 不可拆事务的显式物理参数；不包含 profile 或 route。 */
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
    val threadCount: Int = 3,
    val prepareTimeoutNanos: Long,
    val parkedTimeoutNanos: Long,
    val executionTimeoutNanos: Long,
    val shutdownTimeoutNanos: Long,
)

/** prepareActor 时绑定的物理 Carrier；executeActor 不再接收该对象。 */
@Keep
internal sealed interface Native43499PrimitiveSpec {
    val finalWaiterAddress: Long
    val finalTaskAddress: Long
    val finalLockAddress: Long
    val frameBytes: ByteArray
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
}

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
}
