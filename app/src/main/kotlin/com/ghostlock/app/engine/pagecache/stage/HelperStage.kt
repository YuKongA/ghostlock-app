package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.write.BlockAccess

/** crash_dump64 helper 写入阶段。 */
class HelperStage(
    writer: BlockAccess,
    path: String,
    image: ByteArray,
    key: ByteArray,
    sequence: Int,
    verify: () -> Boolean,
    log: StageLog = silentStageLog,
) : Stage by WriteStage("helper", writer, path, 0L, image, key, sequence, verify, log = log)
