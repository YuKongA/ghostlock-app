package com.ghostlock.app.core.flow

import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState

/** Typed facts produced atomically by one physical Flow step. */
data class FlowStepFacts(
    val primitive: StageResult? = null,
    val scriptDispatch: StageResult? = null,
) {
    init {
        require(primitive != null || scriptDispatch != null) { "step facts must not be empty" }
    }

    /** @return The exact stage fields present in this result. */
    internal fun producedStages(): Set<FlowStage> = buildSet {
        if (primitive != null) add(FlowStage.BACKEND_PRIMITIVE)
        if (scriptDispatch != null) add(FlowStage.ROOT_SCRIPT_DISPATCH)
    }

    /** @return The fact for one declared stage, or null when this result does not own it. */
    internal fun fact(stage: FlowStage): StageResult? = when (stage) {
        FlowStage.BACKEND_PRIMITIVE -> primitive
        FlowStage.ROOT_SCRIPT_DISPATCH -> scriptDispatch
    }

    /** @return True only when every fact in this physical step succeeded. */
    internal fun allSuccessful(): Boolean = listOfNotNull(primitive, scriptDispatch).all { fact ->
        fact.state == StageState.SUCCESS
    }

    companion object {
        /** @return One exact fact for a single-stage physical step. */
        fun single(stage: FlowStage, fact: StageResult): FlowStepFacts = when (stage) {
            FlowStage.BACKEND_PRIMITIVE -> FlowStepFacts(primitive = fact)
            FlowStage.ROOT_SCRIPT_DISPATCH -> FlowStepFacts(scriptDispatch = fact)
        }

        /** @return The same typed fact assigned to every declared producer stage. */
        fun forStages(stages: Set<FlowStage>, fact: StageResult): FlowStepFacts {
            require(stages.isNotEmpty()) { "producer stages must not be empty" }
            return FlowStepFacts(
                primitive = fact.takeIf { FlowStage.BACKEND_PRIMITIVE in stages },
                scriptDispatch = fact.takeIf { FlowStage.ROOT_SCRIPT_DISPATCH in stages },
            )
        }
    }
}

/** Typed step result without arbitrary evidence maps. */
sealed interface StepResult {
    /** Step reached terminal typed facts and every fact may be inspected independently. */
    data class Done(val facts: FlowStepFacts) : StepResult

    /** Step requests another bounded attempt; reason is diagnostic only. */
    data class Retry(val reason: String) : StepResult

    /** Step reached a typed failure or unknown fact. */
    data class Failed(
        val facts: FlowStepFacts,
        val detail: String,
    ) : StepResult
}
