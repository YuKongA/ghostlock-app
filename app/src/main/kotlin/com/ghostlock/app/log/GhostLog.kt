package com.ghostlock.app.log

/**
 * 级别：与 native 侧同一组名字（support/log.hpp 的 level_of 认同样的拼写），这样两端的日志
 * 可以用同一套规则筛选和阅读。
 */
enum class LogLevel(val letter: Char, val token: String) {
    Verbose('V', "verbose"),
    Debug('D', "debug"),
    Info('I', "info"),
    Warn('W', "warn"),
    Error('E', "error"),
}

/**
 * 一条日志的去处。真机上是 logcat；单测与诊断页可以换成别的。
 *
 * @param message 原始消息（logcat 自己会打印级别与标签，所以它要的是这一份）。
 * @param line    已格式化的行（级别字母 + 标签 + 消息）；环与文件去处用这一份。
 */
interface LogSink {
    fun write(level: LogLevel, tag: String, message: String, line: String)
}

/**
 * 应用日志：一个门面，一条格式化规则，若干个去处。
 *
 * 三条规则，按重要程度排：
 *  1. **完整**：级别、标签、时间、消息都留下；错误另有堆栈入口。日志的价值在出事那一刻，不在事后补。
 *  2. **可被 adb 抓走**：真机默认走 logcat（TAG=Ghostlock），设备门禁与排障都从那里取证据。
 *  3. **就地可看**：同时进一个定长环，临时界面直接显示 —— 不用连电脑也能看见发生过什么。
 *
 * 明确不做的：不打印凭据。SA 帧、密钥、SPI 一律不进日志（Kotlin 侧的 SensitiveFrame 连
 * toString 都是 redacted；日志这一层同样不许把它们当普通字段传进来）。
 */
object GhostLog {
    private const val DEFAULT_CAPACITY = 512

    private val lock = Any()
    private val ring = ArrayDeque<String>()
    private var capacity = DEFAULT_CAPACITY
    private var minimum: LogLevel = LogLevel.Info
    private var sinks: List<LogSink> = listOf(LogcatSink, FileSink)

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
            sinks = if (replacement.isEmpty()) listOf(LogcatSink, FileSink) else replacement.toList()
        }
    }

    fun v(tag: String, message: String): Boolean = log(LogLevel.Verbose, tag, message)

    fun d(tag: String, message: String): Boolean = log(LogLevel.Debug, tag, message)

    fun i(tag: String, message: String): Boolean = log(LogLevel.Info, tag, message)

    fun w(tag: String, message: String): Boolean = log(LogLevel.Warn, tag, message)

    fun e(tag: String, message: String, error: Throwable? = null): Boolean {
        val wrote = log(LogLevel.Error, tag, message + (error?.let { " | " + it::class.java.simpleName + ": " + it.message } ?: ""))
        if (error != null) {
            for (frame in error.stackTrace.take(16)) {
                log(LogLevel.Error, tag, "    at " + frame.toString())
            }
        }
        return wrote
    }

    /**
     * 写一条。
     *
     * @return true 当这条达到了最低级别（无论去处接受与否）。
     */
    fun log(level: LogLevel, tag: String, message: String): Boolean {
        if (level.ordinal < synchronized(lock) { minimum }.ordinal) {
            return false
        }
        val line = level.letter + "/" + tag + ": " + message
        val current: List<LogSink>
        synchronized(lock) {
            ring.addLast(line)
            while (ring.size > capacity) {
                ring.removeFirst()
            }
            current = sinks
        }
        for (sink in current) {
            /* logcat 已经会打印 "I/<tag>:"，所以它拿到的是原始消息；环与文件要完整行。 */
            sink.write(level, tag, message, line)
        }
        return true
    }

    /** 环里的内容（最旧在前）。 */
    fun snapshot(): List<String> = synchronized(lock) { ring.toList() }

    fun clear() {
        synchronized(lock) { ring.clear() }
    }
}

/**
 * 真机默认去处：logcat，**用调用方的标签**。
 *
 * 用调用方的标签而不是统一一个：门禁按 GhostlockGate 抓、临时界面按 GhostlockUi 抓，各自
 * adb logcat -s <tag>:V 就能取到自己的那一段，不必事后在混合输出里再筛一遍。
 */
/**
 * 第二个去处：把同一行**追加到设备文件**里 —— 这一份同时是**屏幕上的日志的来源**：
 * native 行写的是同一个 fd（见 [UnifiedLog] 与 [NativeLogBridge]），所以屏幕与
 * `run-as com.ghostlock.app tail files/ghostlock-run.log` 看到的是同一种行。
 *
 * 路径、轮转与开关都在 [LogFile]（候选路径、MAX_BYTES 也在那里）；写失败只当没写成 ——
 * 日志不该把主流程拖下水。
 */
object FileSink : LogSink {
    override fun write(level: LogLevel, tag: String, message: String, line: String) {
        /* 统一去处（2026-10-16 用户裁定：屏幕上的日志必须与 adb 的一样）：与 native 同一个 fd、同一种行格式。 */
        if (UnifiedLog.append(level, line)) {
            return
        }
        /* //TODO(unified-log-20261016): fd 打不开时的旧兜底（候选路径轮询 + epoch 前缀）。它与统一格式
         * 不同形 ⇒ 屏幕读不到；判据全绿、Lead 确认后随 LogFile 的兜底一起清理。 */
        for (candidate in LogFile.CANDIDATES) {
            try {
                val file = java.io.File(candidate)
                file.parentFile?.mkdirs()
                if (file.length() > LogFile.MAX_BYTES) {
                    file.writeText("")
                }
                file.appendText(System.currentTimeMillis().toString() + " " + line + "\n")
                return
            } catch (_: Throwable) {
                /* 换下一个候选；全部失败就只当日志文件不存在（不拖累主流程）。 */
            }
        }
    }
}
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
