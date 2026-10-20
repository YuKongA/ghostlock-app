package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.KotlinSentryPlan
import com.ghostlock.app.engine.pagecache.write.BlockAccess

/** HookStage：写已 patch 的 payload，再写 site 块；回读由调用方 verify 提供。 */
class HookStage(
    private val writer: BlockAccess,
    private val path: String,
    private val plan: KotlinSentryPlan,
    private val payload: ByteArray,
    private val key: ByteArray,
    private val sequence: Int,
    private val log: StageLog = silentStageLog,
    private val verify: () -> Boolean,
) : Stage {
    override suspend fun run(): StageResult {
        log.verbose("HookStage.run", "path" to path, "site" to plan.siteFile,
            "landing" to plan.landingFile, "payload" to payload.size, "sequence" to sequence)
        val written = writer.writeRange(path, plan.landingFile, payload, key, sequence)
        val expected = payload.size / 16
        if (written != expected) {
            log.debug("HookStage.run", "payload-short", "written" to written, "expected" to expected)
            return StageResult(StageCode.HOOK_PARTIAL, "hook-payload-short:" + written + "/" + expected)
        }
        log.debug("HookStage.run", "payload-written", "written" to written)
        val blockStart = plan.siteFile - (plan.siteFile % 16L)
        val block = writer.readBlock(path, blockStart)
        if (block == null) {
            log.debug("HookStage.run", "site-read-null", "blockStart" to blockStart)
            return StageResult(StageCode.HOOK_PARTIAL, "hook-site-read")
        }
        val at = (plan.siteFile - blockStart).toInt()
        for (index in 0 until 4) {
            block[at + index] = ((plan.branchWord ushr (index * 8)) and 0xFF).toByte()
        }
        val siteOk = writer.writeBlock(path, blockStart, block, key, sequence + written)
        if (!siteOk) {
            log.debug("HookStage.run", "site-write-false", "blockStart" to blockStart)
            return StageResult(StageCode.HOOK_PARTIAL, "hook-site-write")
        }
        val verified = runCatching { verify() }.getOrDefault(false)
        if (!verified) {
            log.debug("HookStage.run", "verify-false", "site" to plan.siteFile)
            return StageResult(StageCode.HOOK_PARTIAL, "hook-verify")
        }
        log.debug("HookStage.run", "ok", "written" to written, "site" to plan.siteFile)
        log.verbose("HookStage.run.result", "written" to written, "site" to plan.siteFile, "verified" to true)
        return StageResult(StageCode.OK, evidence = Evidence(
            written = written + 1,
            verified = written + 1,
            site = plan.siteFile,
            landing = plan.landingFile,
            source = "hook",
        ))
    }

    override suspend fun abort(code: StageCode) {
        log.debug("HookStage.abort", "enter", "code" to code)
    }

    override suspend fun close() {
        log.debug("HookStage.close", "done")
    }
}
