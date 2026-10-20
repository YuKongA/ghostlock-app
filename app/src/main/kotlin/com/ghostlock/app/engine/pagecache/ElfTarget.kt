package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.engine.pagecache.stage.StageLog
import com.ghostlock.app.engine.pagecache.stage.silentStageLog
import java.io.File
import java.io.RandomAccessFile

/** 只读 ELF 目标；Kotlin 直接读，不经过 JNI。 */
data class ElfTarget(val path: String, val bytes: ByteArray)

/**
 * 读取目标镜像。
 *
 * App 进程内 Kotlin 与 JNI 同 uid；目标可读性由文件系统/SELinux 决定，JNI 不提升权限。
 * 读不到就返回 null，由调用方记具名原因。
 */
object ElfTargetReader {
    const val MAX_BYTES: Long = 64L * 1024L * 1024L

    fun read(path: String, maxBytes: Long = MAX_BYTES, log: StageLog = silentStageLog): ElfTarget? {
        log.verbose("ElfTargetReader.read", "path" to path, "maxBytes" to maxBytes)
        val file = File(path)
        if (!file.isFile) {
            log.debug("ElfTargetReader.read", "not-file", "path" to path)
            return null
        }
        if (!file.canRead()) {
            log.debug("ElfTargetReader.read", "unreadable", "path" to path)
            return null
        }
        if (file.length() <= 0L || file.length() > maxBytes) {
            log.debug("ElfTargetReader.read", "bad-size", "bytes" to file.length())
            return null
        }
        val result = runCatching {
            RandomAccessFile(file, "r").use { raf ->
                val bytes = ByteArray(file.length().toInt())
                raf.readFully(bytes)
                ElfTarget(path, bytes)
            }
        }.getOrElse { failure ->
            log.debug("ElfTargetReader.read", "exception", "type" to failure.javaClass.simpleName)
            null
        }
        if (result != null) log.debug("ElfTargetReader.read", "ok", "bytes" to result.bytes.size)
        return result
    }
}
