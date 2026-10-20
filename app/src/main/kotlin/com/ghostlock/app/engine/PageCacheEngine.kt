package com.ghostlock.app.engine

import android.content.Context
import com.ghostlock.app.engine.pagecache.stage.StageConfigProvider
import com.ghostlock.app.engine.pagecache.stage.StageLog
import com.ghostlock.app.engine.pagecache.stage.StageResult
import com.ghostlock.app.log.LogLevel

object PageCacheEngine {
    fun run(
        context: Context,
        onEvent: (LogLevel, String) -> Unit,
        modulePatcher: ((ByteArray) -> ByteArray)? = null,
    ): List<String> {
        val log = StageLog { level, message -> onEvent(level, message) }
        log.info("Page cache engine started")
        log.verbose("PageCacheEngine.run", "hasPatcher" to (modulePatcher != null))
        val session = StageConfigProvider.create(context, modulePatcher = modulePatcher, log = log)
        if (session == null) {
            log.error("page cache config unavailable", "config-unavailable")
            return listOf("stage=ENV_MISSING reason=config-unavailable")
        }
        val results = kotlinx.coroutines.runBlocking {
            session.use { runStages(it.config, onEvent) }
        }
        log.verbose("PageCacheEngine.run.result", "results" to results.size)
        log.info("Page cache engine finished")
        return results.map { result -> "stage=" + result.code + " reason=" + result.reason }
    }

    suspend fun runStages(
        config: com.ghostlock.app.engine.pagecache.stage.StageConfig,
        onEvent: (LogLevel, String) -> Unit,
    ): List<StageResult> {
        val log = config.log
        log.info("Stage page cache engine started")
        log.verbose("PageCacheEngine.runStages", "waitPaths" to config.waitPaths.joinToString(","),
            "timeoutMs" to config.waitTimeoutMs)
        val results = com.ghostlock.app.engine.pagecache.stage.StageFactory.build(config).run()
        for (result in results) {
            if (result.code == com.ghostlock.app.engine.pagecache.stage.StageCode.OK) {
                log.debug("PageCacheEngine.runStages", "stage-ok", "reason" to result.reason)
            } else {
                log.warn("stage " + result.code, result.reason.ifBlank { "stage-failed" })
            }
        }
        log.verbose("PageCacheEngine.runStages.result", "results" to results.size)
        log.info("Stage page cache engine finished")
        return results
    }
}
