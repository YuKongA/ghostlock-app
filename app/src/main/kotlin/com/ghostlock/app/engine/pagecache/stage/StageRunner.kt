package com.ghostlock.app.engine.pagecache.stage

/** 顺序执行 Stage；失败/取消走 abort，finally 先 cleanup 再逆序 close。 */
class StageRunner(
    private val stages: List<Stage>,
    private val cleanup: Stage? = null,
    private val log: StageLog = silentStageLog,
) {
    suspend fun run(): List<StageResult> {
        log.verbose("StageRunner.run", "stages" to stages.size, "cleanup" to (cleanup != null))
        val results = mutableListOf<StageResult>()
        var failure: StageCode? = null
        try {
            for (stage in stages) {
                val name = stage.javaClass.simpleName
                log.debug("StageRunner.run", "before", "stage" to name)
                val result = stage.run()
                results += result
                log.debug("StageRunner.run", "after", "stage" to name, "code" to result.code,
                    "reason" to result.reason)
                if (result.code != StageCode.OK) {
                    failure = result.code
                    stage.abort(result.code)
                    break
                }
            }
        } catch (problem: Throwable) {
            log.debug("StageRunner.run", "exception", "type" to problem.javaClass.simpleName)
            failure = StageCode.CANCELLED
            results += StageResult(StageCode.CANCELLED, problem.javaClass.simpleName)
        } finally {
            cleanup?.let { stage ->
                log.debug("StageRunner.run", "cleanup-before")
                results += runCatching { stage.run() }
                    .getOrDefault(StageResult(StageCode.RESTORE_FAILED, "cleanup-crash"))
                log.debug("StageRunner.run", "cleanup-after", "count" to results.size)
            }
            for (stage in stages.asReversed()) {
                log.debug("StageRunner.run", "close", "stage" to stage.javaClass.simpleName)
                runCatching { stage.close() }
            }
            runCatching { cleanup?.close() }
        }
        if (failure != null && results.isEmpty()) results += StageResult(failure)
        log.verbose("StageRunner.run.result", "results" to results.size, "failure" to (failure ?: "none"))
        return results
    }
}
