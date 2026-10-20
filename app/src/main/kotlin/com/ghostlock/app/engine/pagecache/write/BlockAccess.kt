package com.ghostlock.app.engine.pagecache.write

/** Hook/Write 阶段只依赖此接口；真实实现是 BlockWriter。 */
interface BlockAccess {
    val lastError: String get() = ""
    fun writeBlock(path: String, offset: Long, desired: ByteArray, key: ByteArray, sequence: Int): Boolean
    fun writeRange(path: String, offset: Long, image: ByteArray, key: ByteArray, firstSequence: Int): Int
    fun readBlock(path: String, offset: Long): ByteArray?
    fun writeRangeViaHelper(path: String, helperPath: String, offset: Long, image: ByteArray, key: ByteArray, firstSequence: Int): Int = 0
}
