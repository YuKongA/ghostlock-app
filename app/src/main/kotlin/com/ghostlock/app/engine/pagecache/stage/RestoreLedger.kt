package com.ghostlock.app.engine.pagecache.stage

import com.ghostlock.app.engine.pagecache.write.BlockAccess

/** 保存写入前原字节；restore 按逆序写回并回读。 */
class RestoreLedger(private val log: StageLog = silentStageLog) {
    data class Entry(val path: String, val offset: Long, val bytes: ByteArray)

    private val entries = mutableListOf<Entry>()

    fun record(path: String, offset: Long, bytes: ByteArray) {
        log.verbose("RestoreLedger.record", "path" to path, "offset" to offset, "bytes" to bytes.size)
        entries += Entry(path, offset, bytes.copyOf())
        log.debug("RestoreLedger.record", "stored", "entries" to entries.size)
    }

    val size: Int get() = entries.size

    fun restore(writer: BlockAccess, key: ByteArray, sequence: Int): Boolean {
        log.verbose("RestoreLedger.restore", "entries" to entries.size, "sequence" to sequence)
        var seq = sequence
        var ok = true
        for (entry in entries.asReversed()) {
            val written = writer.writeRange(entry.path, entry.offset, entry.bytes, key, seq)
            seq += entry.bytes.size / 16
            val back = writer.readBlock(entry.path, entry.offset)
            if (written != entry.bytes.size / 16 ||
                back == null || !back.contentEquals(entry.bytes.copyOf(16))) {
                log.debug("RestoreLedger.restore", "entry-failed", "path" to entry.path,
                    "offset" to entry.offset, "written" to written)
                ok = false
            } else {
                log.debug("RestoreLedger.restore", "entry-ok", "path" to entry.path, "offset" to entry.offset)
            }
        }
        log.verbose("RestoreLedger.restore.result", "ok" to ok, "entries" to entries.size)
        return ok
    }

    fun clear() {
        log.verbose("RestoreLedger.clear", "entries" to entries.size)
        entries.clear()
    }
}
