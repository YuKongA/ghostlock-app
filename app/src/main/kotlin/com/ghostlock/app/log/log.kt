package com.ghostlock.app.log

/**
 *  级别定义（2026-10-18 用户裁定）：
 *
 *  - [Error]：错误；操作失败且没有回退路径。
 *  - [Warn]：某样东西缺失，但还能回退/降级继续。
 *  - [Info]：某个动作**执行成功**（里程碑，不打印参数细节）。
 *  - [Debug]：分支级日志；每个分支**必须恰好一条**（进入/选择分支时打印）。
 *  - [Verbose]：行级日志；逐行/逐块的细节，只在排查时开。
 *
 *  与 native 侧同一组名字（support/gl_log.h 的 level 认同样的拼写），两端用同一套规则筛选。
 */
enum class LogLevel(val letter: Char, val token: String, val mark: Char) {
    Verbose('V', "verbose", 'V'),
    Debug('D', "debug", '~'),
    Info('I', "info", '*'),
    Warn('W', "warn", '-'),
    Error('E', "error", '!'),
}

/**
 *  一条日志的去处。真机上是 logcat 与文件；单测与诊断页可以换成别的。
 *
 *  @param message 原始消息（logcat 自己会打印级别与标签，所以它要的是这一份）。
 *  @param line    已格式化的行（级别字母 + 标签 + 消息）；环与文件去处用这一份。
 */
interface LogSink {
    fun write(level: LogLevel, tag: String, message: String, line: String)
}

/**
 *  应用日志的**唯一门面**（2026-10-17 用户指令）：
 *
 *  - `log(LogLevel.Info, "…")`  —— 任意级别，等价于 [invoke]；
 *  - `log.info("…")` / `log.debug` / `log.verbose` / `log.warn` / `log.error`；
 *  - 需要分标签时用 `log.invoke(level, tag, msg)`。
 *
 *  本对象只管**级别、格式与环**；落到哪里由 [LogSink] 决定 ——
 *  logcat 在 [LogcatSink]，**写硬盘在 [FileSink]**（委托 [LogFile]）。
 */
object log {
    /** 默认标签（logcat 上按它抓）。 */
    const val DEFAULT_TAG = "Ghostlock"

    private const val DEFAULT_CAPACITY = 512

    /** 启动时刻（首次用到 log 时）：行里的毫秒数都是相对它的。用 nanoTime（单调、纯 JVM，单测可跑）。 */
    private val startedAt: Long = System.nanoTime()

    /** 当前毫秒（启动之后）。 */
    private fun elapsedMs(): Long = (System.nanoTime() - startedAt) / 1_000_000L

    /** 启动后毫秒数（终端时间戳用）。 */
    fun elapsedSinceStartMs(): Long = elapsedMs()

    private val lock = Any()
    private val ring = ArrayDeque<String>()
    private var capacity = DEFAULT_CAPACITY
    private var minimum: LogLevel = LogLevel.Info
    /* 顺序即优先级：**先落盘**（证据），再 logcat（实时 tail / 门禁的次要通道）。 */
    private var sinks: List<LogSink> = listOf(FileSink, LogcatSink)

    /** 设定最低级别与环容量；已记录的环内容保留。 */
    fun configure(minLevel: LogLevel = LogLevel.Info, ringCapacity: Int = DEFAULT_CAPACITY) {
        synchronized(lock) {
            minimum = minLevel
            capacity = if (ringCapacity > 0) ringCapacity else DEFAULT_CAPACITY
            while (ring.size > capacity) {
                ring.removeFirst()
            }
        }
    }

    /** 换掉去处（单测用假去处；诊断页可以再加一个文件去处）。 */
    fun install(vararg replacement: LogSink) {
        synchronized(lock) {
            sinks = if (replacement.isEmpty()) listOf(FileSink, LogcatSink) else replacement.toList()
        }
    }

    /**
     *  写一行（默认标签）。
     *
     *  @return true 当这条达到了最低级别（无论去处接受与否）。
     */
    operator fun invoke(level: LogLevel, message: String): Boolean = invoke(level, DEFAULT_TAG, message)

    /** 写一行（指定标签）。 */
    fun invoke(level: LogLevel, tag: String, message: String): Boolean {
        if (level.ordinal < synchronized(lock) { minimum }.ordinal) {
            return false
        }
        /* 屏显与文件共用的一行（窄屏友好）：<级别 时间> 正文；native 行用方括号。
         * 例：<* 12:00:01.234> tempui: 临时界面已启动 */
//        val stamp = java.text.SimpleDateFormat("HH:mm:ss.SSS", java.util.Locale.US).format(java.util.Date())
        val line = format(level, message)
        val current: List<LogSink>
        synchronized(lock) {
            ring.addLast(line)
            while (ring.size > capacity) {
                ring.removeFirst()
            }
            current = sinks
        }
        for (sink in current) {
            sink.write(level, tag, message, line)
        }
        return true
    }

    /** 与日志行完全相同的格式：<级别标记 > 正文；终端与文件共用这一份。 */
    fun format(level: LogLevel, message: String): String = "<" + level.mark + "> " + message

    fun verbose(message: String): Boolean = invoke(LogLevel.Verbose, message)

    fun debug(message: String): Boolean = invoke(LogLevel.Debug, message)

    fun info(message: String): Boolean = invoke(LogLevel.Info, message)

    fun warn(message: String): Boolean = invoke(LogLevel.Warn, message)

    /** 错误：可选带异常（堆栈逐帧进日志）。 */
    fun error(message: String, problem: Throwable? = null): Boolean {
        val wrote = invoke(LogLevel.Error, DEFAULT_TAG, message + (problem?.let { " | " + it::class.java.simpleName + ": " + it.message } ?: ""))
        if (problem != null) {
            for (frame in problem.stackTrace.take(16)) {
                invoke(LogLevel.Error, DEFAULT_TAG, "    at " + frame.toString())
            }
        }
        return wrote
    }

    /** 环里的内容（最旧在前）。 */
    fun snapshot(): List<String> = synchronized(lock) { ring.toList() }

    fun clear() {
        synchronized(lock) { ring.clear() }
    }
}

/**
 *  真机默认去处之一：logcat，**用调用方的标签**。
 *
 *  用调用方的标签而不是统一一个：门禁按 GhostlockGate 抓、临时界面按 GhostlockUi 抓，各自
 *  adb logcat -s <tag>:V 就能取到自己的那一段，不必事后在混合输出里再筛一遍。
 */
object LogcatSink : LogSink {
    override fun write(level: LogLevel, tag: String, message: String, line: String) {
        when (level) {
            LogLevel.Verbose -> android.util.Log.v(tag, message)
            LogLevel.Debug -> android.util.Log.d(tag, message)
            LogLevel.Info -> android.util.Log.i(tag, message)
            LogLevel.Warn -> android.util.Log.w(tag, message)
            LogLevel.Error -> android.util.Log.e(tag, message)
        }
    }
}
