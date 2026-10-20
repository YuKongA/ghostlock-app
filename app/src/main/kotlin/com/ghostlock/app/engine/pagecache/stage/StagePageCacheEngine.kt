package com.ghostlock.app.engine.pagecache.stage

/** 生产级 Stage 流水线：依赖全部由调用方注入，自身不创建会话。 */
class StagePageCacheEngine(
    private val environment: Stage,
    private val helper: Stage,
    private val module: Stage,
    private val hook: Stage,
    private val trigger: Stage,
    private val wait: Stage,
    private val cleanup: Stage,
    private val log: StageLog = silentStageLog,
) {
    suspend fun run(): List<StageResult> =
        StageRunner(
            stages = listOf(environment, helper, module, hook, trigger, wait),
            cleanup = cleanup,
            log = log,
        ).run()
}
