package com.ghostlock.app.engine.pagecache.stage

import kotlinx.coroutines.delay

/** 轮询 marker；只做等待，不写页缓存。 */
class WaitStage(
    private val paths: List<String>,
    private val timeoutMs: Long = 5000,
    private val intervalMs: Long = 20,
    private val failPaths: List<String> = emptyList(),
    private val exists: (String) -> Boolean = { java.io.File(it).exists() },
    private val log: StageLog = silentStageLog,
    private val sinceMs: Long = 0L,
) : Stage {
    override suspend fun run(): StageResult {
        log.verbose("WaitStage.run", "paths" to paths.joinToString(","),
            "failPaths" to failPaths.joinToString(","), "timeoutMs" to timeoutMs)
        /* Freshness uses the pre-attack epoch; the wait budget starts when this
         * stage actually begins. Mixing them makes a >10s attack time out instantly. */
        val freshSince = if (sinceMs > 0L) sinceMs else System.currentTimeMillis()
        val startedAt = System.currentTimeMillis()
        val deadline = startedAt + timeoutMs
        var staleReported = false
        while (System.currentTimeMillis() < deadline) {
            failPaths.firstOrNull(exists)?.let { hit ->
                val fresh = runCatching { java.io.File(hit).lastModified() >= freshSince }.getOrDefault(true)
                if (fresh) {
                    log.debug("WaitStage.run", "failure-marker", "path" to hit)
                    return StageResult(StageCode.TRIGGER_FAILED, "wait-failure-marker")
                }
            }
            paths.firstOrNull(exists)?.let { hit ->
                /* Script removes the marker at its start; ignore a stale marker
                 * that predates this run. */
                val fresh = runCatching { java.io.File(hit).lastModified() >= freshSince }.getOrDefault(true)
                if (fresh) {
                    log.debug("WaitStage.run", "hit", "path" to hit)
                    return StageResult(StageCode.OK, evidence = Evidence(source = hit))
                }
                if (!staleReported) {
                    staleReported = true
                    log.debug("WaitStage.run", "stale", "path" to hit)
                }
            }
            delay(intervalMs)
        }
        log.debug("WaitStage.run", "timeout", "paths" to paths.joinToString(","))
        return StageResult(StageCode.WAIT_TIMEOUT, "wait-timeout")
    }

    override fun abort(code: StageCode) {
        log.debug("WaitStage.abort", "enter", "code" to code)
    }

    override fun close() {
        log.debug("WaitStage.close", "done")
    }
}
