package com.ghostlock.app.log

/**
 * 统一日志流里的一行：时间戳 + 来源 + 等级 + 正文。
 *
 * 文件里的形状（Kotlin 与 native 同一个形状，屏幕与 `run-as … tail` 因此可以逐字对拍）：
 * ```text
 * 20:51:02.123 K I GhostlockTyped: typed: 43499 library loaded
 * 20:51:02.456 N I pagecache_steps.cpp:285 open path=/system/lib64/libc++.so role=1
 * ```
 *
 * @property stamp 本地时间 HH:mm:ss.SSS；只有内存里的行没有时间戳（[NO_STAMP]）。
 * @property source 'K'＝Kotlin、'N'＝native；认不出来的是 '?'（不丢行）。
 * @property level 等级字母（V/D/I/W/E）；认不出来的是 '?'。
 * @property text 正文（Kotlin 行是 `tag: message`，native 行是 `模块:行号 消息`）。
 */
data class LogLine(val stamp: String, val source: Char, val level: Char, val text: String) {
    /** 同一行的身份：等级 + 正文（时间戳与来源不参与，因为同一行可能只在其中一边有）。 */
    val identity: String get() = level + "|" + text

    /** 屏幕上的这一行（与文件里的那一行逐字相同）。 */
    fun render(): String = stamp + " " + source + " " + level + " " + text

    companion object {
        /** 内存环里的行没有时间戳：占位符排在真实时间戳之前（它们更旧）。 */
        const val NO_STAMP = "--:--:--.---"

        /**
         * 解析文件里的一行。
         *
         * @param raw 一整行（不含换行）。
         * @return 解析结果；形状不认识时原样收下（来源与等级记 '?'）—— 少解析一条也不能丢内容。
         */
        fun parse(raw: String): LogLine {
            if (raw.length >= 17 && raw[8] == '.' && raw[12] == ' ' && raw[14] == ' ') {
                return LogLine(raw.substring(0, 12), raw[13], raw[15], raw.substring(17))
            }
            return LogLine(NO_STAMP, '?', '?', raw)
        }

        /**
         * 把内存环里的一行（`L/tag: message`，见 GhostLog）解析成同一种形状。
         *
         * @return 解析结果；形状不认识时为 null（调用方跳过）。
         */
        fun fromRing(raw: String): LogLine? =
            if (raw.length >= 3 && raw[1] == '/') LogLine(NO_STAMP, 'K', raw[0], raw.drop(2)) else null
    }
}

/**
 * 屏幕上的**统一日志流**：Kotlin 行（[GhostLog]）与 native 行（gl_log.hpp）写进同一个文件，
 * 这里把文件与内存环**合流**后交给界面。
 *
 * 三点必守（用户裁定 2026-10-16）：
 *  1. **一个流**：不做「Kotlin 一堆、native 一堆」，两边按写入顺序落在同一份文件里；
 *  2. **不重复**：文件里的行优先，内存环里已有同一身份的（等级 + 正文）不再显示第二遍；
 *  3. **按时间戳排**：文件本身是追加顺序（时间单调），内存环独有的行按「更旧」排在前面。
 *
 * @note 文件打不开时（理论上的权限问题）自动退回只显示内存环，界面不因此变空。
 */
object UnifiedLog {
    /** 文件侧保留的行数（界面默认只显示其中最后 200 行）。 */
    private const val WINDOW_LINES = 400

    private val tail = LogTail(WINDOW_LINES)

    /** 时间戳格式：与 native 侧 stamp_now 同一形状（本地时间）。 */
    private val stampLock = Any()
    private val stampFormat = java.text.SimpleDateFormat("HH:mm:ss.SSS", java.util.Locale.US)

    @Volatile
    private var nativeStatus: String = "native: 未注册"

    /** native 三个库的注册结果（状态行显示）。 */
    val registration: String get() = nativeStatus

    /** 已经数到的总行数（状态行的「已收 N 行」）。 */
    val lineCount: Int get() = tail.lineCount

    /** 日志文件当前字节数。 */
    val bytes: Long get() = tail.byteCount

    /** 日志文件是否可用（界面据此决定主显示是统一流，还是文件打不开时的兜底列表）。 */
    val fileReady: Boolean get() = LogFile.opened

    /**
     * 启动统一流：打开文件、把 native 接到同一个 fd 上。
     *
     * @return 两行状态（文件 + native），由调用方写进日志流（这样它们也出现在屏幕上）。
     */
    fun start(): List<String> {
        val path = LogFile.open()
        val header = if (path == null) {
            nativeStatus = "native: 未注册（没有可写的日志 fd）"
            listOf("unified: 日志文件不可写（候选都失败）⇒ 屏幕只显示内存环")
        } else {
            listOf("unified: 日志文件 " + path + " fd=" + LogFile.fd)
        }
        if (path != null) {
            nativeStatus = NativeLogBridge.attach(LogFile.fd)
        }
        tail.refresh()
        return header + nativeStatus
    }

    /**
     * Kotlin 行的第二份去处（[FileSink] 调用）：与 native 同一个 fd、同一种行格式。
     *
     * @param level 等级（字母进正文那一格）。
     * @param line  [LogSink] 拿到的整行（`L/tag: message`）；取 `tag: message` 作正文，
     *              这样它与内存环里的那一行是**同一身份**，界面去重能认出重复。
     * @return true 表示已写进文件；false 表示没有 fd（调用方兜底）。
     */
    fun append(level: LogLevel, line: String): Boolean {
        val body = if (line.length >= 2 && line[1] == '/') line.drop(2) else line
        return LogFile.append(stamp() + " K " + level.letter + " " + body)
    }

    /**
     * 统一流（文件 + 内存环，去重、按时间戳排序）。
     *
     * @param max 最多给多少行（默认最后 200 行；界面默认值）。
     * @return 最旧在前、最新在后。
     */
    fun lines(max: Int = 200): List<LogLine> {
        tail.refresh()
        val fromFile = tail.snapshot().map { raw -> LogLine.parse(raw) }
        val identities = fromFile.map { it.identity }.toHashSet()
        val fromRing = GhostLog.snapshot()
            .mapNotNull { raw -> LogLine.fromRing(raw) }
            .filter { it.identity !in identities }
        val merged = fromFile + fromRing
        return merged.sortedBy { it.stamp }.takeLast(if (max > 0) max else merged.size)
    }

    /** 清屏：内存环清空 + 文件从零重来（两处一起清，屏幕与 adb 才不会各显示一半）。 */
    fun clear() {
        GhostLog.clear()
        LogFile.truncate()
        tail.refresh()
    }

    private fun stamp(): String = synchronized(stampLock) { stampFormat.format(java.util.Date()) }
}
