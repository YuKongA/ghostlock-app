package com.ghostlock.app.core.victim

import java.io.File

/**
 * victim 身份证据（通用组件；backend-agnostic）。
 *
 * @property pid victim helper 进程 PID（posix_spawn 返回值）。
 * @property uid 通过 `probeUid` 命令读回的当前 uid（可能是 0/W2 之后）。
 * @property startTimeTicks `/proc/<pid>/stat` 第 22 字段（boot 起 tick 数）；
 *   用于使用方在多次采样之间复查「同一进程未被替换」。
 * @note 本类型只承载物理读回事实，不表达 root/成功。
 * @note 阶段：批 A（task-52）。
 */
internal data class VictimIdentity(
    val pid: Int,
    val uid: Long,
    val startTimeTicks: Long,
)

/**
 * 身份读取失败（具名；fail-closed）。
 *
 * @property reason 稳定可检索文本（`victim-identity:*`）。
 */
internal class VictimIdentityException(val reason: String) : IllegalStateException(reason)

/**
 * 身份证据读取器（纯函数；文件读取可注入，宿主可单测）。
 *
 * @param statReader 读 `/proc/<pid>/stat` 文本的 seam；默认读真实文件。
 * @note 解析规则：comm 可含空格/括号 ⇒ 从最后一个 `)` 之后的第 19 个 token
 *   （整体第 22 字段）取 starttime；解析失败返回 null（不猜）。
 * @note 阶段：批 A。
 */
internal class VictimIdentityReader(
    private val statReader: (Int) -> String? = { pid ->
        runCatching { File("/proc/" + pid + "/stat").readText() }.getOrNull()
    },
) {
    /**
     * 读取 victim 身份。
     *
     * @param pid helper PID（>0）。
     * @param uid probeUid 读回的 uid。
     * @return 身份证据。
     * @throws VictimIdentityException PID 非法/stat 不可读/字段不足。
     */
    fun read(pid: Int, uid: Long): VictimIdentity {
        if (pid <= 0) throw VictimIdentityException("victim-identity:pid")
        if (uid < 0L) throw VictimIdentityException("victim-identity:uid")
        val line = statReader(pid)
            ?: throw VictimIdentityException("victim-identity:stat-missing")
        val closeParen = line.lastIndexOf(')')
        if (closeParen < 0 || closeParen + 2 >= line.length) {
            throw VictimIdentityException("victim-identity:stat-malformed")
        }
        val fields = line.substring(closeParen + 2).trim().split(' ')
        /* fields[0] 是第 3 字段 state；starttime 是第 22 字段 ⇒ 下标 19。 */
        if (fields.size <= 19) {
            throw VictimIdentityException("victim-identity:stat-short")
        }
        val startTimeTicks = fields[19].toLongOrNull()
            ?: throw VictimIdentityException("victim-identity:starttime")
        return VictimIdentity(pid = pid, uid = uid, startTimeTicks = startTimeTicks)
    }
}
