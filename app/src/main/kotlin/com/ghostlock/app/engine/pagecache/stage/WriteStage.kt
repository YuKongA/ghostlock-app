package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.write.BlockAccess

/** 通用写入阶段：写 range + 调用方提供的回读证据。 */
class WriteStage(
    private val name: String,
    private val writer: BlockAccess,
    private val path: String,
    private val offset: Long,
    private val image: ByteArray,
    private val key: ByteArray,
    private val sequence: Int,
    private val verify: () -> Boolean,
    private val helperPath: String? = null,
    private val log: StageLog = silentStageLog,
) : Stage {
    override suspend fun run(): StageResult {
        log.verbose("WriteStage.run", "name" to name, "path" to path, "offset" to offset,
            "bytes" to image.size, "sequence" to sequence, "helper" to (helperPath ?: "none"))
        val written = if (helperPath != null) {
            log.debug("WriteStage.run", "via-helper", "helper" to helperPath)
            writer.writeRangeViaHelper(path, helperPath, offset, image, key, sequence)
        } else {
            log.debug("WriteStage.run", "direct", "path" to path)
            writer.writeRange(path, offset, image, key, sequence)
        }
        val expected = image.size / 16
        if (written != expected) {
            log.debug("WriteStage.run", "short", "written" to written, "expected" to expected,
                "lastError" to writer.lastError)
            return StageResult(StageCode.MODULE_PARTIAL,
                name + "-short:" + written + "/" + expected + " " + writer.lastError)
        }
        val ok = verify()
        if (!ok) {
            log.debug("WriteStage.run", "verify-failed", "name" to name, "lastError" to writer.lastError)
            return StageResult(StageCode.HELPER_VERIFY, name + "-verify " + writer.lastError)
        }
        log.debug("WriteStage.run", "ok", "name" to name, "written" to written)
        log.verbose("WriteStage.run.result", "name" to name, "written" to written, "verified" to true)
        return StageResult(StageCode.OK, evidence = Evidence(written = written, verified = written, source = name))
    }

    override suspend fun abort(code: StageCode) {
        log.debug("WriteStage.abort", "enter", "code" to code)
    }

    override suspend fun close() {
        log.debug("WriteStage.close", "done")
    }
}
