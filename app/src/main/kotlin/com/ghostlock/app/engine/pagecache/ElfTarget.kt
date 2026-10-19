package com.ghostlock.app.engine.pagecache

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

    fun read(path: String, maxBytes: Long = MAX_BYTES): ElfTarget? = runCatching {
        val file = File(path)
        if (!file.isFile || !file.canRead()) return null
        if (file.length() <= 0L || file.length() > maxBytes) return null
        RandomAccessFile(file, "r").use { raf ->
            val bytes = ByteArray(file.length().toInt())
            raf.readFully(bytes)
            ElfTarget(path, bytes)
        }
    }.getOrNull()
}
