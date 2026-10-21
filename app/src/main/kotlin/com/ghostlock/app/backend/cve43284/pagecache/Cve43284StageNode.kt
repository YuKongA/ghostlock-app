package com.ghostlock.app.backend.cve43284.pagecache

import com.ghostlock.app.core.engine.RunNode
import com.ghostlock.app.core.engine.RunNodeResult
import com.ghostlock.app.core.engine.RunPolicy
import com.ghostlock.app.core.engine.SkipPolicy
import com.ghostlock.app.engine.pagecache.stage.Stage
import com.ghostlock.app.engine.pagecache.stage.StageCode
import com.ghostlock.app.engine.pagecache.stage.StageResult

/** 43284 stage as a PipelineEngine node; no retry, no skipped emission. */
class Cve43284StageNode(
    private val stage: Stage,
    override val key: String,
    override val token: String,
) : RunNode {
    override val backendId: String = BACKEND
    override val policy: RunPolicy = RunPolicy(attempts = 1, skipPolicy = SkipPolicy.None)
    @Volatile var lastResult: StageResult? = null; private set

    override suspend fun run(): RunNodeResult {
        val result = stage.run()
        lastResult = result
        return toNodeResult(result)
    }

    override fun abort(reason: String) { stage.abort(StageCode.CANCELLED) }
    override fun close() { stage.close() }

    private fun toNodeResult(result: StageResult): RunNodeResult {
        val evidence = mapOf(
            "written" to result.evidence.written.toString(),
            "verified" to result.evidence.verified.toString(),
            "site" to result.evidence.site.toString(),
            "landing" to result.evidence.landing.toString(),
            "source" to result.evidence.source,
        )
        return when (result.code) {
            StageCode.OK -> RunNodeResult.Done(evidence)
            StageCode.ENV_MISSING, StageCode.TRIGGER_FAILED, StageCode.WAIT_TIMEOUT ->
                RunNodeResult.Refused(result.code.name + ":" + result.reason, evidence)
            StageCode.HELPER_VERIFY, StageCode.MODULE_PARTIAL, StageCode.HOOK_PARTIAL,
            StageCode.RESTORE_FAILED -> RunNodeResult.Frozen(result.code.name + ":" + result.reason, evidence)
            StageCode.CANCELLED -> RunNodeResult.Frozen("stage-cancelled:" + result.reason, evidence)
        }
    }

    private companion object { const val BACKEND = "cve_2026_43284" }
}
