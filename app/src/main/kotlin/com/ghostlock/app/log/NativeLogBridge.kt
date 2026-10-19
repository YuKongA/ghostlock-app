package com.ghostlock.app.log

/**
 * native 日志的 fd 注册：`gl_log.hpp` 的 `set_export_fd`（每个库一个 JNI 入口）。
 *
 * 为什么是三个入口而不是一个：注册位是头文件里**内联函数的函数内静态量**，随 DSO 各复制一份
 * （native 侧同一个方法名若出现在两个库里，JVM 只会绑到先找到的那一个），所以「一个库一份注册」
 * 必须由**各自库里的独立符号**承担 —— 加载一个库就注册一次。
 *
 * @note 库不在（例如某个 backend 没打包）不是错误：那一路本来就不会产生 native 日志。
 */
object NativeLogBridge {
    /** 旧的帧边界库（libghostlock_jni.so）的注册入口。 */
    external fun attachFdLegacy(fd: Int, debug: Boolean): Int

    /** 43499 backend 库（libghostlock43499.so）的注册入口。 */
    external fun attachFd43499(fd: Int, debug: Boolean): Int

    /** 43284 backend 库（libghostlock43284.so）的注册入口。 */
    external fun attachFd43284(fd: Int, debug: Boolean): Int

    /** 三个库的名字（与各自 Kotlin 包装类里的 LIBRARY 一致；这里只用名字，不借用那些类）。 */
    private val LIBRARIES = listOf("ghostlock_jni", "ghostlock43499", "ghostlock43284")

    /**
     * 逐个加载并注册同一个 fd。
     *
     * @param fd 应用打开的追加写 fd（见 [LogFile]）；-1 表示不注册。
     * @return 一行状态（哪个库接上了、哪个不在），可直接写进日志流。
     */
    fun attach(fd: Int): String {
        if (fd < 0) {
            return "native: 未注册（没有可写的日志 fd）"
        }
        val outcomes = mutableListOf<String>()
        for (library in LIBRARIES) {
            val outcome = runCatching {
                System.loadLibrary(library)
                /* debug 构建才在行里带“启动后毫秒数”（release 不显示），native 侧同一开关。 */
                val debug = com.ghostlock.app.BuildConfig.DEBUG
                when (library) {
                    "ghostlock_jni" -> attachFdLegacy(fd, debug)
                    "ghostlock43499" -> attachFd43499(fd, debug)
                    else -> attachFd43284(fd, debug)
                }
            }
            /* 三条 JNI 入口返回的是**上一次**注册的 fd（首次必为 -1），所以状态行报「接上没有」，
             * 不报那个数（-1 会被读成失败）。 */
            outcomes += library + outcome.fold({ "=ok" }, { problem -> "=" + problem.javaClass.simpleName })
        }
        return "native: attach fd=" + fd + " " + outcomes.joinToString(" ")
    }
}
