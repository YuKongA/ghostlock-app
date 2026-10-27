package com.ghostlock.app.backend.cve43499.owned

import com.ghostlock.app.jni.Native43499ActorExecuteResult
import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni

/**
 * `executeActor` 的具名结局；token 不被 execute 消费。
 *
 * @note [Executed] 只表示「窗口已打开且 consumer 观察到有效写」这一物理事实；
 *   不表达 uid/root 等业务成功（那属于 victim/finish 验收）。
 */
internal sealed interface ActorExecuteOutcome {
    /** 物理执行完成：窗口打开、Carrier disarm、consumer 成功计数大于 0。 */
    data class Executed(val receipt: Native43499ActorExecuteResult) : ActorExecuteOutcome

    /** 有效回执但未观察到有效写（窗口未开或 consumer_success=0）。 */
    data class CleanFailure(val receipt: Native43499ActorExecuteResult) : ActorExecuteOutcome

    /** dirty/indeterminate：Native 保留闭包到进程退出；后续 close 只会报 retained。 */
    data class Dirty(
        val receipt: Native43499ActorExecuteResult,
        val reason: String = "execute-dirty",
    ) : ActorExecuteOutcome

    /** JNI 已调用 core 但回执不完整：fail-stop，不重试、不 close、不信任回执。 */
    data class ReceiptIncomplete(
        val reason: String = "execute-receipt-incomplete",
    ) : ActorExecuteOutcome

    /** JNI 前置拒绝（未调用 core）：状态不变。 */
    data object NotAccepted : ActorExecuteOutcome

    /** wrapper 本地拒绝（已转移/已关闭/重复 execute）。 */
    data class Rejected(val reason: String) : ActorExecuteOutcome
}

/**
 * Actor 的唯一 Kotlin owner：独占 Carrier、三个 joinable worker 与 PI 状态，
 * 并在 [closeDetailed] 中按 Native 冻结顺序终结整条所有权链。
 *
 * @param pageContentGeneration prepareActor 回执里的 Page 生成号（对拍事实）。
 * @param primitiveKind prepareActor 回执里实际绑定的 Carrier 种类。
 * @note 阶段：G4L-5；只由 [PageOwner.prepareActor] 创建。
 * @note `execute` 恰好一次：重复调用在 wrapper 本地拒绝（不依赖 Native 状态猜测）。
 * @note token 永不公开；dirty 时 close 会成为 retained 语义而不重试。
 */
internal class ActorOwner private constructor(
    private val pageContentGeneration: Long,
    private val primitiveKind: Int,
) : TokenOwner(OwnerKind.ACTOR) {
    /** @return prepareActor 回执里的 Page 生成号（与 PageOwner 事实对拍）。 */
    internal val boundPageContentGeneration: Long get() = pageContentGeneration

    /** @return prepareActor 回执里实际绑定的 Primitive 种类。 */
    internal val boundPrimitiveKind: Int get() = primitiveKind

    /** execute 是否已被本 wrapper 调用过一次（恰好一次门）。 */
    private var executeCalled: Boolean = false

    /**
     * 执行 prepare 时已绑定的 Carrier。
     *
     * @return 具名结局；token 不被消费，Actor 仍可 cancel/close。
     * @note 不读取/不传递 spec 或 kind；Carrier 选择在 prepare 阶段已冻结。
     * @note 只有 Native 未写回输出时返回 [ActorExecuteOutcome.NotAccepted]。
     * @note 阶段：G4L-5 / Actor 的唯一一次执行窗口。
     */
    internal fun execute(): ActorExecuteOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded ActorExecuteOutcome.Rejected("actor-not-open:" + describe())
        }
        if (executeCalled) {
            return@guarded ActorExecuteOutcome.Rejected("actor-execute-already-called")
        }
        executeCalled = true
        val receipt = Native43499ActorExecuteResult()
        val jniStatus = Native43499Jni.executeActor(tokenLocked(), receipt)
        when (classifyIntStatus(jniStatus)) {
            NativeCallState.NOT_ACCEPTED -> return@guarded ActorExecuteOutcome.NotAccepted
            NativeCallState.RECEIPT_INCOMPLETE ->
                return@guarded ActorExecuteOutcome.ReceiptIncomplete()
            NativeCallState.ACCEPTED -> Unit
        }
        if (receipt.retainedForProcessExit ||
            receipt.effect == Native43499Effect.DIRTY ||
            receipt.effect == Native43499Effect.INDETERMINATE
        ) {
            return@guarded ActorExecuteOutcome.Dirty(
                receipt,
                "execute-" + Native43499Effect.name(receipt.effect),
            )
        }
        val observedWrite = receipt.windowOpened && receipt.consumerSuccessCount > 0L
        if (receipt.callStatus == Native43499CallStatus.OK && observedWrite) {
            return@guarded ActorExecuteOutcome.Executed(receipt)
        }
        return@guarded ActorExecuteOutcome.CleanFailure(receipt)
    }

    /**
     * 请求停止新触发并排空 Carrier 操作；**不消费 token、不销毁 Actor**。
     *
     * @return 具名结局；成功后仍可 close。
     * @note 阶段：G4L-5。
     */
    internal fun cancel(): OwnerCancelOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded OwnerCancelOutcome.Rejected("actor-not-open:" + describe())
        }
        TokenOwner.settleCancel(this) { handle, receipt ->
            Native43499Jni.cancelActor(handle, receipt)
        }
    }

    /**
     * 按 Native 冻结顺序终结 Actor、Carrier 及其拥有的 Page；接受后消费 token。
     *
     * @return 具名结局；[OwnerCloseOutcome.Retained] 表示 Native 保留闭包到进程退出。
     * @note 已转移/已关闭的调用是本地拒绝，不触 Native。
     * @note 阶段：G4L-5 终结。
     */
    internal override fun closeDetailed(): OwnerCloseOutcome = guarded {
        if (lifecycleLocked() != Lifecycle.OPEN || tokenLocked() == 0L) {
            return@guarded OwnerCloseOutcome.Rejected("actor-not-open:" + describe())
        }
        TokenOwner.settleClose(this) { handle, receipt: Native43499CloseResult ->
            Native43499Jni.closeActor(handle, receipt)
        }
    }

    companion object {
        /**
         * 从 prepareActor 回执收养一个 Actor token（只由 [PageOwner.prepareActor] 调用）。
         *
         * @param token Native 返回的非零 Actor token。
         * @param pageContentGeneration prepareActor 回执里的 page_content_generation。
         * @param primitiveKind prepareActor 回执里的 primitive_kind。
         * @return 新 owner；token 为 0 或安装失败时返回 null（调用方负责 closeActor）。
         */
        internal fun adopt(
            token: Long,
            pageContentGeneration: Long,
            primitiveKind: Int,
        ): ActorOwner? {
            if (token == 0L) return null
            val owner = ActorOwner(pageContentGeneration, primitiveKind)
            return if (owner.adoptToken(token)) owner else null
        }
    }
}
