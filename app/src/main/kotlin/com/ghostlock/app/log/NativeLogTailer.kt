package com.ghostlock.app.log

import java.io.File

/**
 * native 行的实时出口：tail 应用日志文件里由 native 侧写入的行。
 *
 * native 行格式（gl_log.hpp）：`HH:MM:SS.mmm <V|D|I> file:line body`。
 * Kotlin 行以 `<标记>` 开头，不匹配这里的时间戳前缀，因此不会被重复显示。
 */
object NativeLogTailer {
    private val linePattern = Regex("^\\[([VDIWE~*!-]) (?:\\d+ms|\\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\] (.*)$")

    /** 解析一行 native 日志；不是 native 行时返回 null。 */
    fun parse(line: String): Pair<LogLevel, String>? {
        val match = linePattern.matchEntire(line) ?: return null
        val mark = match.groupValues[1].firstOrNull() ?: return null
        val level = LogLevel.entries.firstOrNull { it.mark == mark } ?: return null
        return level to match.groupValues[2]
    }

    /** 从当前文件尾开始 tail；返回可停止的句柄。 */
    fun start(path: String?, emit: (LogLevel, String) -> Unit): Handle {
        val handle = Handle()
        if (path == null) return handle
        val file = File(path)
        handle.thread = Thread({
            var offset = if (file.isFile) file.length() else 0L
            while (handle.running) {
                val length = if (file.isFile) file.length() else 0L
                if (length > offset) {
                    runCatching {
                        java.io.RandomAccessFile(file, "r").use { reader ->
                            reader.seek(offset)
                            val bytes = ByteArray((length - offset).toInt())
                            reader.readFully(bytes)
                            val text = String(bytes, Charsets.UTF_8)
                            val complete = text.substringBeforeLast('\n', text)
                            offset += complete.toByteArray(Charsets.UTF_8).size
                            complete.lineSequence().forEach { line ->
                                parse(line)?.let { (level, body) -> emit(level, body) }
                            }
                        }
                    }
                }
                Thread.sleep(120)
            }
        }, "native-log-tail").apply { isDaemon = true }
        handle.thread?.start()
        return handle
    }

    class Handle internal constructor() {
        @Volatile var running = true
        internal var thread: Thread? = null

        fun stop() {
            running = false
            thread?.join(400)
        }
    }
}
