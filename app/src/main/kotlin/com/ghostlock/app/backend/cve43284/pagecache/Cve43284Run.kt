package com.ghostlock.app.backend.cve43284.pagecache

import com.ghostlock.app.core.engine.PipelineEngine
import com.ghostlock.app.core.engine.PipelineResult
import com.ghostlock.app.engine.pagecache.stage.Stage
import com.ghostlock.app.engine.pagecache.stage.StageFactory
import com.ghostlock.app.engine.pagecache.stage.StageStages

/** 43284 execution over PipelineEngine; StageFactory provides the nodes. */
class Cve43284Run(
    private val stages: StageStages,
) {
    constructor(config: com.ghostlock.app.engine.pagecache.stage.StageConfig) : this(StageFactory.stages(config))

    suspend fun run(): PipelineResult = execute().first

    /** Compatibility view for PageCacheEngine logging; synthetic stopped nodes are not included. */
    suspend fun runStages(): List<com.ghostlock.app.engine.pagecache.stage.StageResult> = execute().second

    private suspend fun execute(): Pair<PipelineResult, List<com.ghostlock.app.engine.pagecache.stage.StageResult>> {
        val nodes = stages.nodes.mapIndexed { index, stage ->
            Cve43284StageNode(stage, KEY_ORDER[index], KEY_ORDER[index])
        }
        val cleanup = Cve43284StageNode(stages.cleanup, "cleanup", "cleanup")
        val engine = PipelineEngine()
        val result = try {
            engine.run(nodes, cleanup)
        } finally {
            engine.close()
        }
        val results = nodes.mapNotNull { it.lastResult } + listOfNotNull(cleanup.lastResult)
        return result to results
    }

    private companion object {
        val KEY_ORDER = listOf("environment", "helper", "module", "hook", "trigger", "wait")
    }
}
