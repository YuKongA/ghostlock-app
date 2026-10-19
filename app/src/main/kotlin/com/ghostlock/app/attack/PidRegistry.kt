package com.ghostlock.app.attack

import android.system.Os
import android.system.OsConstants
import java.io.File

/**
 * 执行进程的子进程登记与清理。
 *
 * JNI 侧若 fork/exec 子进程，应把 pid 交给这里登记；中止时按 uid 区分：
 *  - 同 uid：Os.kill(SIGKILL) 直接可杀；
 *  - 不同 uid（root 子进程）：只能 `su -c kill -9`，没有 root 通道就放弃。
 */
object PidRegistry {
    private val registered = mutableSetOf<Int>()
    private val lock = Any()

    fun register(pid: Int) {
        if (pid <= 0) return
        synchronized(lock) { registered += pid }
    }

    fun unregister(pid: Int) {
        synchronized(lock) { registered -= pid }
    }

    /**
     * 杀掉 [rootPid] 的全部后代（含登记过的 pid）。
     *
     * @return 实际发出 SIGKILL 的 pid 列表（同 uid）；root 子进程走 su，不在此列表。
     */
    fun killDescendants(rootPid: Int): List<Int> {
        val tree = descendants(rootPid)
        val extra = synchronized(lock) { registered.toList() }
        val killed = mutableListOf<Int>()
        for (pid in (tree + extra).distinct()) {
            if (pid <= 0 || pid == rootPid) continue
            val uid = uidOf(pid) ?: continue
            if (uid == android.os.Process.myUid()) {
                runCatching { Os.kill(pid, OsConstants.SIGKILL) }
                    .onSuccess { killed += pid }
            } else {
                runCatching {
                    ProcessBuilder("su", "-c", "kill -9 " + pid)
                        .redirectErrorStream(true)
                        .start()
                        .waitFor(2, java.util.concurrent.TimeUnit.SECONDS)
                }
            }
        }
        synchronized(lock) { registered.clear() }
        return killed
    }

    /** 通过 /proc/<pid>/stat 的 ppid 字段构建后代列表。 */
    fun descendants(rootPid: Int): List<Int> {
        val parents = mutableMapOf<Int, Int>()
        File("/proc").listFiles()?.forEach { dir ->
            val pid = dir.name.toIntOrNull() ?: return@forEach
            val stat = File(dir, "stat").takeIf { it.isFile }?.readText() ?: return@forEach
            val close = stat.lastIndexOf(')')
            if (close < 0) return@forEach
            val fields = stat.substring(close + 2).split(' ')
            val ppid = fields.getOrNull(1)?.toIntOrNull() ?: return@forEach
            parents[pid] = ppid
        }
        val out = mutableListOf<Int>()
        var frontier = listOf(rootPid)
        while (frontier.isNotEmpty()) {
            val next = mutableListOf<Int>()
            for (parent in frontier) {
                for ((pid, ppid) in parents) {
                    if (ppid == parent && pid !in out) {
                        out += pid
                        next += pid
                    }
                }
            }
            frontier = next
        }
        return out
    }

    private fun uidOf(pid: Int): Int? = runCatching {
        File("/proc/$pid/status").readLines()
            .firstOrNull { it.startsWith("Uid:") }
            ?.split('\t', ' ')
            ?.getOrNull(1)
            ?.toIntOrNull()
    }.getOrNull()
}
