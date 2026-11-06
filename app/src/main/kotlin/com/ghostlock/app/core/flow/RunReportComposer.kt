package com.ghostlock.app.core.flow

import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptExitKind
import com.ghostlock.app.core.rootscript.RootScriptFinishSeverity
import com.ghostlock.app.core.rootscript.RootScriptItemResult
import com.ghostlock.app.core.rootscript.RootScriptItemState
import com.ghostlock.app.core.rootscript.RootScriptResult
import com.ghostlock.app.core.rootscript.RootScriptResultReason
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.FinishItemResult
import com.ghostlock.app.domain.run.ItemState
import com.ghostlock.app.domain.run.PayloadItemResult
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState

/** Typed result of the single GLR1 decode attempt. */
sealed interface RootScriptDecodeOutcome {
    /** Dispatch did not succeed, so the result object must not be read. */
    data object NotAttempted : RootScriptDecodeOutcome

    /** Exact complete result decoded from the pre-created object. */
    data class Complete(val result: RootScriptResult) : RootScriptDecodeOutcome

    /** Same-object identity was replaced before decode. */
    data object IdentityMismatch : RootScriptDecodeOutcome

    /** Object identity remained stable but the complete grammar was invalid. */
    data object Malformed : RootScriptDecodeOutcome
}

/** Composes backend facts and one GLR1 outcome into the sole six-layer product report. */
class RunReportComposer {
    /**
     * Applies the frozen Payload/Finish aggregate rules and terminal priority.
     *
     * @param plan Same immutable plan used by renderer and decoder.
     * @param backend Backend-owned primitive, dispatch, cleanup, and held-state facts.
     * @param decode Single result-object decode disposition.
     * @param cancelled Whether a safe cancellation linearized before product completion.
     * @return Complete PRODUCT_ROOT report.
     */
    fun compose(
        plan: RootScriptExecutionPlan,
        backend: BackendRunFacts,
        decode: RootScriptDecodeOutcome,
        cancelled: Boolean = false,
    ): RunReport {
        plan.validate()
        val product = when (decode) {
            RootScriptDecodeOutcome.NotAttempted -> notAttempted(plan)
            RootScriptDecodeOutcome.IdentityMismatch -> failedDecode(plan, identityMismatch = true)
            RootScriptDecodeOutcome.Malformed -> failedDecode(plan, identityMismatch = false)
            is RootScriptDecodeOutcome.Complete -> complete(plan, decode.result)
        }
        val terminal = terminal(
            plan = plan,
            backend = backend,
            product = product,
            decode = decode,
            cancelled = cancelled,
        )
        return RunReport(
            runCapability = RunCapability.PRODUCT_ROOT,
            diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
            backendPrimitive = backend.primitive,
            rootScriptDispatch = backend.scriptDispatch,
            payloadItems = product.payloadItems,
            payloadOverall = product.payloadOverall,
            finishItems = product.finishItems,
            finishOverall = product.finishOverall,
            backendCleanup = backend.cleanup,
            terminal = terminal,
        )
    }

    /** Synthesizes requested-but-not-started items without reading an empty result object. */
    private fun notAttempted(plan: RootScriptExecutionPlan): ProductFacts = ProductFacts(
        payloadItems = plan.payload.map { item ->
            PayloadItemResult(item.itemId, ItemState.NOT_RUN, ResultReason.NOT_STARTED, ExitKind.NONE, 0)
        },
        payloadOverall = StageResult.SKIPPED,
        finishItems = plan.finish.map { item ->
            FinishItemResult(item.itemId, ItemState.NOT_RUN, ResultReason.NOT_STARTED, ExitKind.NONE, 0)
        },
        finishOverall = StageResult.SKIPPED,
        durableHandoff = false,
    )

    /** Produces a whole-result failure without salvaging a partial prefix. */
    private fun failedDecode(plan: RootScriptExecutionPlan, identityMismatch: Boolean): ProductFacts {
        val itemReason = ResultReason.MALFORMED_RESULT
        val overall = if (identityMismatch) {
            StageResult(StageState.FAILED, ResultReason.ARTIFACT_IDENTITY_MISMATCH, ExitKind.NONE, 0)
        } else {
            StageResult(StageState.UNKNOWN, ResultReason.MALFORMED_RESULT, ExitKind.NONE, 0)
        }
        return ProductFacts(
            payloadItems = plan.payload.map { item ->
                PayloadItemResult(item.itemId, ItemState.UNKNOWN, itemReason, ExitKind.NONE, 0)
            },
            payloadOverall = overall,
            finishItems = plan.finish.map { item ->
                FinishItemResult(item.itemId, ItemState.UNKNOWN, itemReason, ExitKind.NONE, 0)
            },
            finishOverall = overall,
            durableHandoff = false,
        )
    }

    /** Maps a strict complete GLR1 result and computes deterministic aggregates. */
    private fun complete(plan: RootScriptExecutionPlan, result: RootScriptResult): ProductFacts {
        require(result.runId == plan.runId && result.generation == plan.generation) {
            "decoded result identity disagrees with plan"
        }
        val payloadItems = result.payloadItems.map { item -> item.toPayloadItem() }
        val finishItems = result.finishItems.map { item -> item.toFinishItem() }
        val requiredTrigger = plan.payload.zip(payloadItems).firstOrNull { (definition, item) ->
            definition.required && item.state in setOf(ItemState.FAILED, ItemState.UNKNOWN, ItemState.CANCELLED)
        }?.second
        val payloadOverall = requiredTrigger?.toStageResult() ?: StageResult.SUCCESS
        val finishOverall = finishAggregate(finishItems, result.durableHandoff)
        return ProductFacts(
            payloadItems = payloadItems,
            payloadOverall = payloadOverall,
            finishItems = finishItems,
            finishOverall = finishOverall,
            durableHandoff = result.durableHandoff,
        )
    }

    /** Applies `UNKNOWN > FAILED > CANCELLED > SKIPPED > SUCCESS`, treating durable reboot as success. */
    private fun finishAggregate(items: List<FinishItemResult>, durableHandoff: Boolean): StageResult {
        if (items.isEmpty()) return StageResult.SUCCESS
        val effective = items.map { item ->
            if (durableHandoff && item.state == ItemState.DISPATCHING) {
                item.copy(state = ItemState.SUCCESS)
            } else {
                item
            }
        }
        return when {
            effective.any { item -> item.state == ItemState.UNKNOWN } ->
                effective.first { item -> item.state == ItemState.UNKNOWN }.toStageResult()
            effective.any { item -> item.state == ItemState.FAILED } ->
                effective.first { item -> item.state == ItemState.FAILED }.toStageResult()
            effective.any { item -> item.state == ItemState.CANCELLED } ->
                effective.first { item -> item.state == ItemState.CANCELLED }.toStageResult()
            effective.any { item -> item.state == ItemState.SKIPPED_DEPENDENCY } -> StageResult.SKIPPED
            effective.all { item -> item.state == ItemState.SUCCESS } -> StageResult.SUCCESS
            else -> StageResult(StageState.UNKNOWN, ResultReason.INTERNAL_CONTRACT_FAILURE, ExitKind.NONE, 0)
        }
    }

    /** Applies the parent wire's fixed product terminal priority from top to bottom. */
    private fun terminal(
        plan: RootScriptExecutionPlan,
        backend: BackendRunFacts,
        product: ProductFacts,
        decode: RootScriptDecodeOutcome,
        cancelled: Boolean,
    ): RunTerminal {
        if (product.durableHandoff) return RunTerminal.REBOOT_REQUESTED
        if (backend.terminalHint in setOf(
                BackendTerminalHint.REBOOT_REQUIRED,
                BackendTerminalHint.UNKNOWN_HELD_STATE,
            )
        ) {
            return RunTerminal.REBOOT_REQUIRED
        }
        val safetyFinishFailed = plan.finish.zip(product.finishItems).any { (definition, item) ->
            definition.severity == RootScriptFinishSeverity.SAFETY_CRITICAL && item.state == ItemState.FAILED
        }
        if (backend.cleanup.state == StageState.FAILED || safetyFinishFailed) return RunTerminal.NEEDS_ATTENTION
        val optionalUnknown = plan.payload.zip(product.payloadItems).any { (definition, item) ->
            !definition.required && item.state == ItemState.UNKNOWN
        }
        val anyUnknown = decode in setOf(
            RootScriptDecodeOutcome.IdentityMismatch,
            RootScriptDecodeOutcome.Malformed,
        ) || optionalUnknown || listOf(
            backend.primitive.state,
            backend.scriptDispatch.state,
            backend.cleanup.state,
            product.payloadOverall.state,
            product.finishOverall.state,
        ).any { state -> state == StageState.UNKNOWN }
        if (anyUnknown) return RunTerminal.UNKNOWN
        if (cancelled) return RunTerminal.CANCELLED
        if (
            backend.primitive.state == StageState.FAILED ||
            backend.scriptDispatch.state == StageState.FAILED ||
            product.payloadOverall.state == StageState.FAILED
        ) {
            return RunTerminal.FAILED
        }
        val optionalDegraded = plan.payload.zip(product.payloadItems).any { (definition, item) ->
            !definition.required && item.state in setOf(ItemState.FAILED, ItemState.CANCELLED)
        }
        val normalFinishFailed = plan.finish.zip(product.finishItems).any { (definition, item) ->
            definition.severity == RootScriptFinishSeverity.NORMAL && item.state == ItemState.FAILED
        }
        if (optionalDegraded || normalFinishFailed) return RunTerminal.DEGRADED
        if (
            backend.primitive.state == StageState.SUCCESS &&
            backend.scriptDispatch.state == StageState.SUCCESS &&
            product.payloadOverall.state == StageState.SUCCESS &&
            product.finishOverall.state == StageState.SUCCESS &&
            backend.cleanup.state == StageState.SUCCESS
        ) {
            return RunTerminal.SUCCESS
        }
        return RunTerminal.UNKNOWN
    }

    /** Maps one strict root-script item into the stable domain representation. */
    private fun RootScriptItemResult.toPayloadItem(): PayloadItemResult = PayloadItemResult(
        itemId = itemId,
        state = state.toDomain(),
        reason = reason.toDomain(),
        exitKind = exitKind.toDomain(),
        exitValue = exitValue,
    )

    /** Maps one strict root-script item into the stable domain representation. */
    private fun RootScriptItemResult.toFinishItem(): FinishItemResult = FinishItemResult(
        itemId = itemId,
        state = state.toDomain(),
        reason = reason.toDomain(),
        exitKind = exitKind.toDomain(),
        exitValue = exitValue,
    )

    /** Converts one item result to its aggregate stage representation. */
    private fun PayloadItemResult.toStageResult(): StageResult = StageResult(
        state = state.toStageState(),
        reason = reason,
        exitKind = exitKind,
        exitValue = exitValue,
    )

    /** Converts one item result to its aggregate stage representation. */
    private fun FinishItemResult.toStageResult(): StageResult = StageResult(
        state = state.toStageState(),
        reason = reason,
        exitKind = exitKind,
        exitValue = exitValue,
    )

    /** Maps strict GLR1 item state to the public domain enum. */
    private fun RootScriptItemState.toDomain(): ItemState = ItemState.entries.first { item ->
        item.wireValue == wireValue
    }

    /** Maps strict GLR1 reason to the public domain enum. */
    private fun RootScriptResultReason.toDomain(): ResultReason = ResultReason.entries.first { reason ->
        reason.wireValue == wireValue
    }

    /** Maps strict GLR1 exit kind to the public domain enum. */
    private fun RootScriptExitKind.toDomain(): ExitKind = ExitKind.entries.first { kind ->
        kind.wireValue == wireValue
    }

    /** Maps an item state to the closest aggregate stage state. */
    private fun ItemState.toStageState(): StageState = when (this) {
        ItemState.NOT_RUN -> StageState.NOT_RUN
        ItemState.DISPATCHING -> StageState.RUNNING
        ItemState.SUCCESS -> StageState.SUCCESS
        ItemState.FAILED -> StageState.FAILED
        ItemState.SKIPPED_DEPENDENCY -> StageState.SKIPPED
        ItemState.UNKNOWN -> StageState.UNKNOWN
        ItemState.CANCELLED -> StageState.CANCELLED
    }

    /** Internal product facts before backend terminal priority is applied. */
    private data class ProductFacts(
        val payloadItems: List<PayloadItemResult>,
        val payloadOverall: StageResult,
        val finishItems: List<FinishItemResult>,
        val finishOverall: StageResult,
        val durableHandoff: Boolean,
    )
}
