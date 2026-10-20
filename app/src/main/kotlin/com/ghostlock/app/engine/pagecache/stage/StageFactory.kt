package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.KotlinSentryPlan
import com.ghostlock.app.engine.pagecache.write.BlockAccess

data class WriteSpec(
    val path: String,
    val image: ByteArray,
    val key: ByteArray,
    val sequence: Int,
    val verify: () -> Boolean,
    val viaHelper: Boolean = false,
)

data class HookSpec(
    val path: String,
    val plan: KotlinSentryPlan,
    val payload: ByteArray,
    val key: ByteArray,
    val sequence: Int,
    val verify: () -> Boolean,
)

/** 由调用方提供全部数据；Factory 不创建 SA、不读配置、不持有会话。 */
data class StageConfig(
    val environment: EnvironmentInput,
    val block: BlockAccess,
    val helper: WriteSpec,
    val module: WriteSpec,
    val hook: HookSpec,
    val waitPaths: List<String>,
    val trigger: suspend () -> Boolean,
    val restore: suspend () -> Boolean,
    val waitTimeoutMs: Long = 5000,
    val failPaths: List<String> = emptyList(),
    val log: StageLog = silentStageLog,
    val waitSince: Long = 0L,
)

private const val CRASH_DUMP = "/apex/com.android.runtime/bin/crash_dump64"

object StageFactory {
    fun build(config: StageConfig): StagePageCacheEngine = StagePageCacheEngine(
        environment = EnvironmentStage(config.environment, config.log),
        helper = HelperStage(
            config.block, config.helper.path, config.helper.image,
            config.helper.key, config.helper.sequence, config.helper.verify, config.log,
        ),
        module = ModuleStage(
            config.block, config.module.path, config.module.image,
            config.module.key, config.module.sequence, config.module.verify,
            if (config.module.viaHelper) CRASH_DUMP else null, config.log,
        ),
        hook = HookStage(
            config.block, config.hook.path, config.hook.plan, config.hook.payload,
            config.hook.key, config.hook.sequence, config.log, config.hook.verify,
        ),
        trigger = TriggerStage(config.trigger, config.log),
        wait = WaitStage(
            paths = config.waitPaths,
            timeoutMs = config.waitTimeoutMs,
            failPaths = config.failPaths,
            log = config.log,
            sinceMs = config.waitSince,
        ),
        cleanup = CleanupStage(config.log, config.restore),
        log = config.log,
    )
}
