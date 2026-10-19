package com.ghostlock.app.engine.pagecache

import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileDescriptor

/**
 * LKM 驻留窗口：打开 `/dev/glk` 并把 fd 握在手里就是**占住窗口**，关掉 fd 就是 UNLOAD。
 *
 * # 依据
 * 参照实现 `ghostlock-app-ref@5f184760:src/core/backend/cve_2026_43284/steps/chain.cpp:362-380`
 * 的 `finish()`：钩子恢复之后关闭窗口（close 回调发 UNLOAD），open/close 各一次，
 * 关窗失败不掩盖主结果。我们的模块自己打印
 * `ghostlock: resident (session-bound; fd close ends the window)`
 * （`tools/lkm/ghostlock/ghostlock.glk.c:447`），并且通道是 single-open（同文件 :328），
 * 所以「持 fd = 占用窗口」在本机成立。
 *
 * # 幂等
 * - 已开着再 [open] ⇒ 不再开第二个 fd（single-open 的模块会给第二个 -EBUSY），返回 true；
 * - 没开就 [close] ⇒ 什么都不关，返回 true（窗口本来就是关的）；
 * - [open] → [close] → [open] ⇒ 允许（窗口是会话绑定的，不是一次性的）。
 *
 * # 失败语义
 * 每一步失败只写一行日志（带 errno 名）并返回 false，**从不抛异常**；
 * 调用方不得把返回值当控制流判据 —— 与参照实现「countermeasure fails soft」一致。
 *
 * @param defaultPath [open] 未显式给路径时用的设备节点。
 * @param emit        逐行日志回调（默认丢弃）。
 * @param openFn      打开缝：成功返回 fd，失败抛 [ErrnoException]（单测注入用）。
 * @param closeFn     关闭缝：失败抛 [ErrnoException]（单测注入用）。
 */
class LkmWindow43284(
    private val defaultPath: String = DEFAULT_PATH,
    private val emit: (String) -> Unit = {},
    private val openFn: (String) -> FileDescriptor = ::openGlkDevice,
    private val closeFn: (FileDescriptor) -> Unit = ::closeGlkDevice,
) {
    /** 持有的设备 fd；null = 窗口没开。 */
    private var device: FileDescriptor? = null

    /** 当前窗口对应的路径（[open] 实际用的那个）。 */
    private var openPath: String = defaultPath

    /**
     * 打开窗口（幂等）。
     *
     * @param path 设备节点，默认 [DEFAULT_PATH]。
     * @return true = 窗口现在处于打开状态（本次打开或本来就开着）；
     *         false = 打不开（原因已按 `lkm window: opened=0 … errno=…` 进日志）。
     */
    @Synchronized
    fun open(path: String = defaultPath): Boolean {
        if (device != null) {
            emit("lkm window: opened=1 path=" + openPath)
            return true
        }
        val opened = try {
            openFn(path)
        } catch (problem: Throwable) {
            emit("lkm window: opened=0 path=" + path + " errno=" + errnoName(problem))
            return false
        }
        device = opened
        openPath = path
        emit("lkm window: opened=1 path=" + path)
        return true
    }

    /**
     * 关闭窗口（幂等）。
     *
     * @return true = 窗口现在关着（本次关掉或本来就没开）；
     *         false = close 失败（fd 还在手里，可以重试；原因已进日志）。
     */
    @Synchronized
    fun close(): Boolean {
        val current = device
        if (current == null) {
            emit("lkm window: closed=0")
            return true
        }
        return try {
            closeFn(current)
            device = null
            emit("lkm window: closed=1")
            true
        } catch (problem: Throwable) {
            /* close 失败（EINTR 等）时 fd 可能还占着窗口：留在手里，允许重试。 */
            emit("lkm window: closed=0 errno=" + errnoName(problem))
            false
        }
    }

    /** @return 窗口当前是否被本对象占着（fd 有效）。 */
    @Synchronized
    fun isOpen(): Boolean = device != null

    companion object {
        /** 模块注册的会话绑定设备节点。 */
        const val DEFAULT_PATH: String = "/dev/glk"

        /** 常用 errno 名（设备上优先用 [OsConstants.errnoName]；JVM 单测里它是桩）。 */
        private val ERRNO_NAMES: Map<Int, String> = mapOf(
            1 to "EPERM", 2 to "ENOENT", 3 to "ESRCH", 4 to "EINTR", 5 to "EIO",
            6 to "ENXIO", 9 to "EBADF", 11 to "EAGAIN", 12 to "ENOMEM", 13 to "EACCES",
            14 to "EFAULT", 16 to "EBUSY", 17 to "EEXIST", 18 to "EXDEV", 19 to "ENODEV",
            20 to "ENOTDIR", 21 to "EISDIR", 22 to "EINVAL", 23 to "ENFILE", 24 to "EMFILE",
            25 to "ENOTTY", 26 to "ETXTBSY", 27 to "EFBIG", 28 to "ENOSPC", 29 to "ESPIPE",
            30 to "EROFS", 31 to "EMLINK", 32 to "EPIPE", 36 to "ENAMETOOLONG",
            38 to "ENOSYS", 39 to "ENOTEMPTY", 40 to "ELOOP", 75 to "EOVERFLOW",
            95 to "EOPNOTSUPP", 110 to "ETIMEDOUT",
        )

        /**
         * 只读探测节点是否存在（不打开设备，不占窗口）。
         *
         * @param path 设备节点。
         * @return true = 节点存在；任何异常都按 false 处理。
         */
        fun present(path: String = DEFAULT_PATH): Boolean =
            runCatching { File(path).exists() }.getOrDefault(false)

        /**
         * errno ⇒ 名字（日志用）。
         *
         * @param errno 正的 errno 值。
         * @return 设备上取 `OsConstants.errnoName`；JVM 单测的桩不可用时退回常用表，最后退回 `E<errno>`。
         */
        internal fun errnoName(errno: Int): String {
            val system = runCatching { OsConstants.errnoName(errno) }.getOrNull()
            if (!system.isNullOrEmpty()) {
                return system
            }
            return ERRNO_NAMES[errno] ?: ("E" + errno)
        }

        /**
         * @param problem 打开/关闭抛出的异常。
         * @return [ErrnoException] 的 errno 名；其它异常退化为 `类名:消息`。
         */
        internal fun errnoName(problem: Throwable): String =
            (problem as? ErrnoException)?.let { failure -> errnoName(failure.errno) }
                ?: (problem.javaClass.simpleName + ":" + (problem.message ?: ""))
    }
}

/**
 * 生产打开缝：O_RDWR | O_CLOEXEC（与 `tools/glkctl/glkctl.c:41` 同参数）。
 *
 * @param path 设备节点。
 * @return fd；失败抛 [ErrnoException]（errno 名进日志）。
 */
private fun openGlkDevice(path: String): FileDescriptor =
    Os.open(path, OsConstants.O_RDWR or OsConstants.O_CLOEXEC, 0)

/**
 * 生产关闭缝：关掉 fd（模块在 release 里发 UNLOAD）。
 *
 * @param fd 打开时拿到的 fd。
 */
private fun closeGlkDevice(fd: FileDescriptor) {
    Os.close(fd)
}
