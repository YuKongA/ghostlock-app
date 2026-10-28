package com.ghostlock.app.jni

import androidx.annotation.Keep

/**
 * CVE-2026-98260 的 JNI 机械面（B2）。
 *
 * 只在 **Shizuku UserService 进程**（uid 2000 / shell 域）内加载/调用；app 进程不使用。
 * 职责边界（delta §2）：
 * - `spawnStatic`：`posix_spawn` + `POSIX_SPAWN_SETSID` 启动独立 payload，stdout/stderr 重定向到日志文件；
 * - `waitExit`：仅对**非 parked** 短命令做有界 `waitpid(WNOHANG)` 轮询；
 * - **无 kill/signal/fork API**：payload 一旦 armed 只能 park，重启是唯一清理。
 *
 * @note 真机门禁 G8/G9 未过（device-gate-pending）；JNI 失败以负 errno/具名码返回，不抛异常。
 */
@Keep
internal object Native98260Jni {
    /** 机械面 ABI 版本；与 C 侧 `GLK98260_JNI_API_VERSION` 必须相等。 */
    internal const val API_VERSION: Int = 1

    /** `waitExit` 超时码（与 C 侧一致；不代表进程失败）。 */
    internal const val TIMEOUT: Int = -1003

    init {
        System.loadLibrary("ghostlock98260jni")
    }

    /** @return C 侧机械面 ABI 版本；调用方启动时校验。 */
    external fun nativeApiVersion(): Int

    /**
     * 启动一个独立 payload 进程。
     *
     * @param path payload 绝对路径（已由 service 侧写入 /data/local/tmp）。
     * @param argv 参数表（非空、≤32 项、每项非空 ≤1024 B）；`argv[0]` 必须是可执行路径/名字。
     * @param outPath 日志文件路径（O_CREAT|O_TRUNC）。
     * @return >0=pid；<0=-errno 或输入非法。调用方不得在 armed 后据此 kill。
     */
    external fun spawnStatic(path: String, argv: Array<String>, outPath: String): Int

    /**
     * 接上 app 日志 fd（项目 gl_log 管线；内部 dup+CLOEXEC）。
     *
     * @param fd service 经 `ParcelFileDescriptor.detachFd()` 得到的 raw fd；调用后 Kotlin 立即关闭它。
     * @note 只写日志；失败静默（日志失败不影响攻击）。R1b / G14。
     */
    external fun attachLogFd(fd: Int)

    /** 断开 app 日志 fd（destroy；关闭 gl_log 的 dup）。 */
    external fun detachLogFd()

    /**
     * 写一行到项目日志管线（logcat + app fd；一行一次 write）。
     *
     * @param level 0=verbose..4=error（越界按 info）。
     * @param msg ASCII/UTF-8 单行正文（禁密钥）。
     */
    external fun logLine(level: Int, msg: String)

    /**
     * 有界等待子进程退出（WNOHANG 轮询，**不发信号**）。
     *
     * @param pid spawnStatic 返回值。
     * @param timeoutMs 等待窗口（≤600000）。
     * @return ≥0=退出码（128+sig=信号终止）；[TIMEOUT]=窗口耗尽（不得 kill）；其余负值=-errno。
     */
    external fun waitExit(pid: Int, timeoutMs: Int): Int
}
