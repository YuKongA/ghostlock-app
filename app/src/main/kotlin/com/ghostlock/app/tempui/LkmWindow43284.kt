package com.ghostlock.app.tempui

import java.io.File

/**
 * LKM 驻留窗口：打开 `/dev/glk` 并持有 fd 就占住窗口，关掉 fd 就是 UNLOAD。
 *
 * 依据 `ghostlock-app-ref@5f184760:steps/chain.cpp:362-380`：`finish()` 在**恢复钩子之后**
 * 关闭窗口（close 回调发 UNLOAD），open/close 各一次，失败不掩盖主结果。
 * 我们的模块自己打印过 `ghostlock: resident (session-bound; fd close ends the window)`，
 * 所以"持 fd = 占用窗口"这条在本机成立。
 *
 * 幂等：重复 open 不再开；未开时 close 返回 true；open→close→open 允许。
 *
 * @param path 设备节点路径
 * @param openFn 打开用的缝（单测注入；返回 -1 表示失败）
 * @param closeFn 关闭用的缝
 */
class LkmWindow43284(
    private val path: String = DEFAULT_PATH,
    private val openFn: (String) -> Int = { p -> runCatching { java.io.RandomAccessFile(p, "rw").fd }.getOrNull()?.let { 1 } ?: -1 },
    private val closeFn: (Int) -> Boolean = { true },
) {
    private var opened = false

    /** @return 窗口是否处于占用状态（已开或本次打开成功）。 */
    fun open(): Boolean {
        if (opened) {
            return true
        }
        val fd = openFn(path)
        if (fd < 0) {
            return false
        }
        opened = true
        return true
    }

    /** @return 关闭是否成功；未打开时视为成功（幂等）。 */
    fun close(): Boolean {
        if (!opened) {
            return true
        }
        val ok = closeFn(1)
        if (ok) {
            opened = false
        }
        return ok
    }

    /** @return 当前是否占用窗口。 */
    fun isOpen(): Boolean = opened

    /** 设备节点路径（与模块一致）。 */
    companion object {
        const val DEFAULT_PATH: String = "/dev/glk"

        /** @return 设备上是否存在该节点（只读探测，失败即 false）。 */
        fun present(path: String = DEFAULT_PATH): Boolean = runCatching { File(path).exists() }.getOrDefault(false)
    }
}
