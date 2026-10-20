package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.write.BlockAccess

/** vendor carrier KO 写入阶段。 */
class ModuleStage(
    writer: BlockAccess,
    path: String,
    image: ByteArray,
    key: ByteArray,
    sequence: Int,
    verify: () -> Boolean,
    helperPath: String? = null,
    log: StageLog = silentStageLog,
) : Stage by WriteStage("module", writer, path, 0L, image, key, sequence, verify, helperPath, log)
