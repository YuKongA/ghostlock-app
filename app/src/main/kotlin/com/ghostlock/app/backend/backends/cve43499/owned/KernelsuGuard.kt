package com.ghostlock.app.backend.backends.cve43499.owned

import java.io.File

/**
 * KSU 守卫结局（G07）：positive signal 即拒绝（fail-closed）。
 *
 * @note 读不到不等于“没加载”（app 域在 Enforcing 下常读不到 `/proc/modules`）：
 *   unreadable 只记录，不作为 loaded 证据；任何 positive signal 才拒绝。
 * @note 阶段：P1a parity（task-63）；真机行为 UNVERIFIED（设备离线）。
 */
internal sealed interface KernelsuGuardResult {
    /**
     * 未观察到 KSU 加载证据。
     *
     * @property evidence 逐信号读数（`absent`/`unreadable`/`present:*`）。
     */
    data class Absent(val evidence: Map<String, String>) : KernelsuGuardResult

    /**
     * 观察到 KSU 加载证据 ⇒ 调用方必须拒绝 W1。
     *
     * @property signal 命中信号（`proc-modules`/`sys-module`/`adb-paths` 以 `+` 连接）。
     * @property evidence 逐信号读数。
     */
    data class Loaded(
        val signal: String,
        val evidence: Map<String, String>,
    ) : KernelsuGuardResult
}

/**
 * G07：KernelSU 加载守卫（legacy `ksu_root_owned`/module-visible 的 Kotlin 侧等价）。
 *
 * 检查三类**进程外**证据（不做 `su -c id -u`，不在 app 域 fork/exec）：
 * 1) `/proc/modules` 行首模块名为 `kernelsu`/`ksu`；
 * 2) `/sys/module/ksu` 或 `/sys/module/kernelsu` 存在；
 * 3) `/data/adb/ksu` 或 `/data/adb/ksud` 存在。
 *
 * 任一命中 ⇒ [KernelsuGuardResult.Loaded]（Flow 在 W1 前拒绝并留 evidence）；
 * 全部未命中/不可读 ⇒ [KernelsuGuardResult.Absent]（记录读数，不把 unreadable 当 loaded）。
 *
 * @param procModulesReader `/proc/modules` 读取器（返回 null=不可读）。
 * @param sysModuleExists `/sys/module/<name>` 存在性判定。
 * @param pathExists 绝对路径存在性判定。
 * @note 全部 reader 可注入；本类不写文件、不加载模块、无全局状态。
 * @note 阶段：P1a parity（task-63）；真机（mondrian userspace 残留/内核 absent）判定已由
 *   Lead 2026-10-27 裁定“kernel 侧 absent 即满足”；本守卫按 positive signal fail-closed。
 */
internal class KernelsuGuard(
    private val procModulesReader: () -> String? = { readTextOrNull("/proc/modules") },
    private val sysModuleExists: (String) -> Boolean = { name ->
        runCatching { File("/sys/module/" + name).exists() }.getOrDefault(false)
    },
    private val pathExists: (String) -> Boolean = { path ->
        runCatching { File(path).exists() }.getOrDefault(false)
    },
) {
    /**
     * 执行一次 KSU 加载检查。
     *
     * @return Absent（附读数）或 Loaded（附命中信号与读数）。
     */
    fun check(): KernelsuGuardResult {
        val evidence = linkedMapOf<String, String>()
        val modulesText = runCatching { procModulesReader() }.getOrNull()
        val moduleNames = modulesText?.lineSequence()
            ?.map { line -> line.trimStart().substringBefore(' ').substringBefore('\t') }
            ?.filter { it.isNotEmpty() }
            ?.toSet()
            .orEmpty()
        val moduleLoaded = moduleNames.contains("kernelsu") || moduleNames.contains("ksu")
        evidence["procModules"] = when {
            modulesText == null -> "unreadable"
            moduleLoaded -> "loaded"
            else -> "absent"
        }
        val sysLoaded = sysModuleExists("ksu") || sysModuleExists("kernelsu")
        evidence["sysModule"] = if (sysLoaded) "loaded" else "absent"
        val adbPaths = KSU_USERS_SPACE_PATHS.filter(pathExists)
        evidence["adbPaths"] = if (adbPaths.isEmpty()) {
            "absent"
        } else {
            "present:" + adbPaths.joinToString(",")
        }
        val signals = buildList {
            if (moduleLoaded) add("proc-modules")
            if (sysLoaded) add("sys-module")
            if (adbPaths.isNotEmpty()) add("adb-paths")
        }
        return if (signals.isEmpty()) {
            KernelsuGuardResult.Absent(evidence)
        } else {
            KernelsuGuardResult.Loaded(
                signal = signals.joinToString("+"),
                evidence = evidence,
            )
        }
    }

    private companion object {
        /** KSU userspace 证据路径（legacy `/data/adb/ksu|ksud`）。 */
        val KSU_USERS_SPACE_PATHS: List<String> = listOf("/data/adb/ksu", "/data/adb/ksud")

        /**
         * @param path 绝对路径。 @return trim 文本；不存在/不可读返回 null。
         */
        fun readTextOrNull(path: String): String? =
            runCatching { File(path).readText() }.getOrNull()
    }
}
