package com.ghostlock.app.jni

/**
 * memfd 的薄 JNI 入口：日志与 asset 输入共用同一个匿名内存对象。
 *
 * 所有返回值遵循 native 惯例：非负为字节数/0，负数为 -errno；Kotlin 侧统一压成
 * [Int] 或 Boolean，由调用方决定失败语义。
 */
object NativeMemfd {
    /** 创建匿名 memfd；失败返回负 errno。 */
    fun create(): Int {
        if (!Native.ensureLoaded()) return -1
        return runCatching { createNative() }.getOrDefault(-1)
    }

    /** 从 [offset] 读入 [buffer]；返回字节数或负 errno。 */
    fun read(fd: Int, buffer: ByteArray, offset: Long): Int =
        runCatching { readNative(fd, buffer, offset) }.getOrDefault(-1)

    /** 从 [offset] 写入 [bytes]；返回字节数或负 errno。 */
    fun write(fd: Int, bytes: ByteArray, offset: Long): Int =
        runCatching { writeNative(fd, bytes, offset) }.getOrDefault(-1)

    /** 刷数据到持久存储；true 表示成功。 */
    fun sync(fd: Int): Boolean = runCatching { syncNative(fd) == 0 }.getOrDefault(false)

    /** 关闭 fd；true 表示成功。 */
    fun close(fd: Int): Boolean = runCatching { closeNative(fd) == 0 }.getOrDefault(false)

    private external fun createNative(): Int
    private external fun readNative(fd: Int, buffer: ByteArray, offset: Long): Int
    private external fun writeNative(fd: Int, bytes: ByteArray, offset: Long): Int
    private external fun syncNative(fd: Int): Int
    private external fun closeNative(fd: Int): Int
}
