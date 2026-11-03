package com.ghostlock.app.backend.backends.cve43499.owned

/**
 * G05 prebuilt repair 链状态机（P1b-2；与 active chains 分离，最多 1 个）。
 *
 * 生命周期（Kotlin 权威；Native 零改动/零 ABI）：
 * ```
 * EMPTY → PLACEMENT_READY → PAGE_READY → ACTIVATED → CONSUMED
 *             │                  │
 *             ├─ clean discard ──┴→ DISCARDED
 *             └─ dirty/retained ───→ RETAINED（不 free/不伪报）
 * ```
 * - 预建在 W1 成功出口（早于 W1b/W2；见 review §8.10 对 Q1=B 差异的登记）；
 * - `activate()` 只把 Page owner 交给 Lifecycle 入列（尾部新 chain），不预建 Actor；
 * - `discard()` 关闭 placement/page 并给出 [Cve43499PrebuiltDiscardReceipt]（B2 判定依据）。
 *
 * @param createdNanos 创建时间（`elapsedRealtimeNanos`；由 caller 采样，宿主可注入 fake）。
 * @note 不入 `Cve43499Lifecycle.chains`；`closeAll` 顺序 active→prebuilt→victim。
 * @note 阶段：G05 v3 实现（task-70 + Lead delta）；无真机验证。
 */
internal enum class Cve43499PrebuiltRepairState {
    EMPTY,
    PLACEMENT_READY,
    PAGE_READY,
    ACTIVATED,
    CONSUMED,
    DISCARDED,
    RETAINED,
}

/**
 * prebuilt 链 discard/close 回执（B2 clean vs dirty 判定依据）。
 *
 * @property clean true=全部 owner 干净释放且无 retained。
 * @property retained true=存在 retained/dirty 闭包（不 free/不伪报）。
 * @property termination 具名终结证据（page/placement 各自的 close 结局）。
 */
internal data class Cve43499PrebuiltDiscardReceipt(
    val clean: Boolean,
    val retained: Boolean,
    val termination: String,
)

/**
 * prebuilt landing 结局（owned 层内部，交给 Lifecycle 映射 typed step）。
 *
 * @property Landed.reclaimSends landing 回执 reclaim send 数。
 */
internal sealed interface PrebuiltLandingOutcome {
    data class Landed(val reclaimSends: Long) : PrebuiltLandingOutcome
    data class CleanFailure(val reason: String) : PrebuiltLandingOutcome
    data class Dirty(val reason: String) : PrebuiltLandingOutcome
    data class Rejected(val reason: String) : PrebuiltLandingOutcome
}

/**
 * 单条 prebuilt repair 链的 owner 容器 + 状态机（最多 1 个实例由 Lifecycle 持有）。
 *
 * @param createdNanos 创建时间（单调 ns）。
 */
internal class Cve43499PrebuiltRepair(
    val createdNanos: Long,
) {
    private var placement: PlacementOwner? = null
    private var page: PageOwner? = null
    private var state: Cve43499PrebuiltRepairState = Cve43499PrebuiltRepairState.EMPTY
    private var retainedFlag: Boolean = false

    /** @return 当前状态（证据用）。 */
    val currentState: Cve43499PrebuiltRepairState get() = state

    /** @return true=存在 retained/dirty 闭包。 */
    val retained: Boolean get() = retainedFlag || state == Cve43499PrebuiltRepairState.RETAINED

    /** @return true=PAGE_READY（可 activation 的最小事务）。 */
    val available: Boolean get() = state == Cve43499PrebuiltRepairState.PAGE_READY

    /**
     * placement prepare 成功后挂载 owner。
     *
     * @param owner 已 prepared 的 PlacementOwner。
     * @throws IllegalStateException 非 EMPTY（状态机滥用）。
     */
    fun attachPlacement(owner: PlacementOwner) {
        check(state == Cve43499PrebuiltRepairState.EMPTY) { "prebuilt:state:$state" }
        placement = owner
        state = Cve43499PrebuiltRepairState.PLACEMENT_READY
    }

    /**
     * landing 成功后把 placement 转移为 page。
     *
     * @param owner 已 landed 的 PageOwner。
     * @throws IllegalStateException 非 PLACEMENT_READY。
     */
    fun attachPage(owner: PageOwner) {
        check(state == Cve43499PrebuiltRepairState.PLACEMENT_READY) { "prebuilt:state:$state" }
        placement = null
        page = owner
        state = Cve43499PrebuiltRepairState.PAGE_READY
    }

    /**
     * 消费 PLACEMENT_READY 的 placement 落页（PAGE_READY；失败保持 holder 供 discard）。
     *
     * @param config landing 参数与最终字节。
     * @return typed prebuilt landing 结局。
     */
    fun land(config: LandingConfig): PrebuiltLandingOutcome {
        if (state != Cve43499PrebuiltRepairState.PLACEMENT_READY) {
            return PrebuiltLandingOutcome.Rejected("state:" + state.name.lowercase())
        }
        val owner = placement ?: return PrebuiltLandingOutcome.Rejected("placement-missing")
        return when (val outcome = owner.landPage(config)) {
            is LandingOutcome.Landed -> {
                placement = null
                page = outcome.owner
                state = Cve43499PrebuiltRepairState.PAGE_READY
                PrebuiltLandingOutcome.Landed(outcome.receipt.reclaimSendCount)
            }
            is LandingOutcome.CleanFailure ->
                PrebuiltLandingOutcome.CleanFailure(
                    "landing:clean-failure:call=" +
                        Native43499CallStatus.name(outcome.receipt.callStatus) +
                        ":errno=" + outcome.receipt.systemError,
                )
            is LandingOutcome.ConsumedDirty -> {
                placement = null
                markRetained("landing:dirty")
                PrebuiltLandingOutcome.Dirty("landing:dirty")
            }
            is LandingOutcome.Rejected ->
                PrebuiltLandingOutcome.Rejected(outcome.reason)
            LandingOutcome.NotAccepted ->
                PrebuiltLandingOutcome.Rejected("not-accepted")
            is LandingOutcome.ReceiptIncomplete ->
                PrebuiltLandingOutcome.Dirty("receipt-incomplete")
        }
    }

    /**
     * 取走可 activation 的 Page（调用方随后标记 ACTIVATED 并交给 active chain）。
     *
     * @return PageOwner；状态不是 PAGE_READY 返回 null（不 throw，便于具名失败）。
     */
    fun takePageForActivation(): PageOwner? {
        if (state != Cve43499PrebuiltRepairState.PAGE_READY) return null
        val owner = page ?: return null
        page = null
        return owner
    }

    /** 标记 activation 成功（page 已进入 active chain）。 */
    fun markActivated() {
        check(state == Cve43499PrebuiltRepairState.PAGE_READY) { "prebuilt:state:$state" }
        state = Cve43499PrebuiltRepairState.ACTIVATED
    }

    /** 标记最小事务已消费（clean）。 */
    fun markConsumed() {
        state = Cve43499PrebuiltRepairState.CONSUMED
    }

    /**
     * 标记 dirty/retained（不 free；由进程退出兜底）。
     *
     * @param reason retained 具名原因（证据用，可为空）。
     */
    fun markRetained(reason: String = "prebuilt-retained") {
        retainedFlag = true
        state = Cve43499PrebuiltRepairState.RETAINED
        retainedReason = reason
    }

    /** @return retained 具名原因（非 retained 时为空串）。 */
    var retainedReason: String = ""
        private set

    /**
     * clean/dirty discard：关闭 page→placement（有 owner 才 close），返回 B2 判定回执。
     *
     * @return discard 回执；ACTIVATED/CONSUMED 时无本层资源（clean 空回执）。
     */
    fun discard(): Cve43499PrebuiltDiscardReceipt {
        if (state == Cve43499PrebuiltRepairState.DISCARDED ||
            state == Cve43499PrebuiltRepairState.CONSUMED
        ) {
            return Cve43499PrebuiltDiscardReceipt(
                clean = !retainedFlag,
                retained = retainedFlag,
                termination = "prebuilt=already-" + state.name.lowercase() + ";",
            )
        }
        if (state == Cve43499PrebuiltRepairState.ACTIVATED) {
            /* Page 已转移给 active chain；prebuilt 本层无资源。 */
            state = Cve43499PrebuiltRepairState.CONSUMED
            return Cve43499PrebuiltDiscardReceipt(
                clean = true,
                retained = false,
                termination = "prebuilt=activated;",
            )
        }
        var clean = !retainedFlag
        val note = StringBuilder()
        page?.let { owner ->
            val outcome = closeSafely { owner.closeDetailed() }
            note.append("page=").append(closeName(outcome)).append(';')
            if (outcome !is OwnerCloseOutcome.Released) clean = false
        } ?: note.append("page=skipped;")
        placement?.let { owner ->
            val outcome = closeSafely { owner.closeDetailed() }
            note.append("placement=").append(closeName(outcome)).append(';')
            if (outcome !is OwnerCloseOutcome.Released) clean = false
        } ?: note.append("placement=skipped;")
        page = null
        placement = null
        if (!clean) retainedFlag = true
        state = if (clean) {
            Cve43499PrebuiltRepairState.DISCARDED
        } else {
            Cve43499PrebuiltRepairState.RETAINED
        }
        if (!clean && retainedReason.isEmpty()) retainedReason = "prebuilt-discard-retained"
        return Cve43499PrebuiltDiscardReceipt(
            clean = clean,
            retained = !clean,
            termination = "prebuilt=" + (if (clean) "discarded" else "retained") + ";" + note,
        )
    }

    /** 关闭单个 owner 的异常安全包装（异常 ⇒ Rejected；不 throw）。 */
    private fun closeSafely(block: () -> OwnerCloseOutcome): OwnerCloseOutcome =
        runCatching(block).getOrElse { problem ->
            OwnerCloseOutcome.Rejected(
                "close-threw:" + (problem.message ?: problem.javaClass.simpleName),
            )
        }

    /** owner close 结局 → 稳定 token。 */
    private fun closeName(outcome: OwnerCloseOutcome): String = when (outcome) {
        is OwnerCloseOutcome.Released -> "released"
        is OwnerCloseOutcome.Retained -> "retained"
        is OwnerCloseOutcome.ReceiptIncomplete -> "receipt-incomplete"
        OwnerCloseOutcome.NotAccepted -> "not-accepted"
        is OwnerCloseOutcome.Rejected -> "rejected"
    }
}
