package com.ghostlock.app.engine.pagecache.stage

/** 收尾：restore 只执行一次；失败具名，不静默。 */
class CleanupStage(
    private val log: StageLog = silentStageLog,
    private val restore: suspend () -> Boolean,
) : Stage {
    private var done = false

    override suspend fun run(): StageResult {
        if (done) {
            log.debug("CleanupStage.run", "already-done")
            return StageResult(StageCode.OK, evidence = Evidence(restored = true, source = "cleanup"))
        }
        done = true
        log.verbose("CleanupStage.run")
        val ok = runCatching { restore() }.getOrDefault(false)
        if (!ok) {
            log.debug("CleanupStage.run", "restore-false")
            return StageResult(StageCode.RESTORE_FAILED, "restore-failed")
        }
        log.debug("CleanupStage.run", "ok")
        return StageResult(StageCode.OK, evidence = Evidence(restored = true, source = "cleanup"))
    }

    override fun abort(code: StageCode) {
        log.debug("CleanupStage.abort", "enter", "code" to code)
    }

    override fun close() {
        log.debug("CleanupStage.close", "done")
    }
}
