package com.ghostlock.app.engine.pagecache.stage

import kotlinx.coroutines.delay

/** 轮询 marker；只做等待，不写页缓存。 */
class WaitStage(
    private val paths: List<String>,
    private val timeoutMs: Long = 5000,
    private val intervalMs: Long = 20,
    private val exists: (String) -> Boolean = { java.io.File(it).exists() },
    private val log: StageLog = silentStageLog,
) : Stage {
    override suspend fun run(): StageResult {
        log.verbose("WaitStage.run", "paths" to paths.joinToString(","), "timeoutMs" to timeoutMs)
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val hit = paths.firstOrNull(exists)
            if (hit != null) {
                log.debug("WaitStage.run", "hit", "path" to hit)
                return StageResult(StageCode.OK, evidence = Evidence(source = hit))
            }
            delay(intervalMs)
        }
        log.debug("WaitStage.run", "timeout", "paths" to paths.joinToString(","))
        return StageResult(StageCode.WAIT_TIMEOUT, "wait-timeout")
    }

    override suspend fun abort(code: StageCode) {
        log.debug("WaitStage.abort", "enter", "code" to code)
    }

    override suspend fun close() {
        log.debug("WaitStage.close", "done")
    }
}
