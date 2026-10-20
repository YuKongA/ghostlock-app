package com.ghostlock.app.engine.pagecache.stage

import kotlinx.coroutines.delay

/** 触发阶段：只负责调用 trigger，不解释 marker。 */
class TriggerStage(
    private val trigger: suspend () -> Boolean,
    private val log: StageLog = silentStageLog,
) : Stage {
    override suspend fun run(): StageResult {
        log.verbose("TriggerStage.run", "settleMs" to 500)
        /* DFRoot 在 patch_hook 后 usleep(500ms) 才创建 orphan；真机上省掉
         * 这个窗口会让 page-cache 写入尚未稳定就被 init 执行。 */
        delay(500)
        val ok = runCatching { trigger() }.getOrDefault(false)
        if (!ok) {
            log.debug("TriggerStage.run", "trigger-false")
            return StageResult(StageCode.TRIGGER_FAILED, "trigger-failed")
        }
        log.debug("TriggerStage.run", "ok")
        return StageResult(StageCode.OK, evidence = Evidence(source = "trigger"))
    }

    override suspend fun abort(code: StageCode) {
        log.debug("TriggerStage.abort", "enter", "code" to code)
    }

    override suspend fun close() {
        log.debug("TriggerStage.close", "done")
    }
}
