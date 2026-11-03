package com.ghostlock.app.backend.host

import android.content.Context
import java.io.File
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions

/**
 * host 为单次 Flow run 创建的私有目录/路径事实（B4a 附录 §1）。
 *
 * @property uid host 进程 uid。
 * @property pid host 进程 pid。
 * @property domain host SELinux domain（`/proc/self/attr/current`，去 NUL/空白）。
 * @property runDir 本次 run 目录（0700）。
 * @property nativeLibDir host 可见的 nativeLibraryDir。
 * @property logPath run 日志文件（runDir/run.log）。
 * @note 只有 host 能创建/清理；路径经 `host.*` 参数传给 Flow。
 * @note 阶段：B4a。
 */
data class HostRunPaths(
    val uid: Int,
    val pid: Int,
    val domain: String,
    val runDir: File,
    val nativeLibDir: String,
    val logPath: File,
) {
    /**
     * @return `host.*` 参数映射（供 [FlowEntryParams.injectHost]）。
     */
    fun toHostParams(): Map<String, Any?> = linkedMapOf(
        FlowEntryParams.KEY_HOST_UID to uid,
        FlowEntryParams.KEY_HOST_PID to pid,
        FlowEntryParams.KEY_HOST_DOMAIN to domain,
        FlowEntryParams.KEY_HOST_RUN_DIR to runDir.absolutePath,
        FlowEntryParams.KEY_HOST_NATIVE_LIB_DIR to nativeLibDir,
        FlowEntryParams.KEY_HOST_LOG_PATH to logPath.absolutePath,
    )
}

/**
 * 创建 app `:flowhost` 进程的 run 目录。
 *
 * @param context app context（filesDir/nativeLibraryDir）。
 * @param runId 本次 run 唯一 id（[A-Za-z0-9][A-Za-z0-9._-]{0,63}）。
 * @return 路径事实。
 * @throws IllegalArgumentException runId 非法。
 * @throws IllegalStateException 目录创建失败（`flow-host:run-dir:create`）或
 *   nativeLibraryDir 不可解析（`flow-host:native-lib-dir-unavailable`）。
 */
fun createAppHostPaths(context: Context, runId: String): HostRunPaths {
    requireRunId(runId)
    val base = File(File(context.filesDir, "ghostlock-glue"), "flowhost")
    val dir = File(base, runId)
    val createFailure = createSecureDir(dir)
    if (createFailure != null) error(createFailure)
    val nativeLibDir = context.applicationInfo.nativeLibraryDir
    if (nativeLibDir.isNullOrBlank()) error("flow-host:native-lib-dir-unavailable")
    return hostRunPaths(dir, nativeLibDir)
}

/**
 * 创建 Shizuku shell 进程的 run 目录。
 *
 * @param context shell 侧可用的 package context（Shizuku 可能为 null）。
 * @param runId 本次 run 唯一 id。
 * @return 路径事实；nativeLibraryDir 不可解析/目录创建失败返回 null（调用方具名拒绝）。
 * @throws IllegalArgumentException runId 非法。
 */
fun createShellHostPaths(context: Context?, runId: String): HostRunPaths? {
    requireRunId(runId)
    val nativeLibDir = context?.applicationInfo?.nativeLibraryDir?.takeIf { it.isNotBlank() }
        ?: return null
    val dir = File(File("/data/local/tmp", "ghostlock-flow"), runId)
    if (createSecureDir(dir) != null) return null
    return hostRunPaths(dir, nativeLibDir)
}

/**
 * best-effort 清理 run 目录（证据先写入 verify；清理结果记 `host_run_dir_cleanup=ok|failed`）。
 *
 * @param paths 待清理路径。
 * @return true=目录已不存在；false=仍有残留（不改变终态）。
 */
fun cleanupHostPaths(paths: HostRunPaths): Boolean {
    runCatching { paths.runDir.deleteRecursively() }
    return !paths.runDir.exists()
}

/** @return 组装路径事实（身份/日志路径）。 */
private fun hostRunPaths(dir: File, nativeLibDir: String): HostRunPaths = HostRunPaths(
    uid = android.os.Process.myUid(),
    pid = android.os.Process.myPid(),
    domain = readSelfDomain(),
    runDir = dir,
    nativeLibDir = nativeLibDir,
    logPath = File(dir, "run.log"),
)

/**
 * 安全创建 0700 目录（拒绝已存在/symlink；权限失败即失败）。
 *
 * @param dir 目标目录（父目录由 mkdirs 创建）。
 * @return null=成功；否则为具名失败原因（`flow-host:run-dir:*`）。
 */
private fun createSecureDir(dir: File): String? {
    val path = dir.toPath()
    if (Files.isSymbolicLink(path)) return "flow-host:run-dir:symlink"
    if (Files.exists(path)) return "flow-host:run-dir:exists"
    if (!dir.mkdirs()) return "flow-host:run-dir:create"
    val permissionSet = runCatching {
        Files.setPosixFilePermissions(
            path,
            PosixFilePermissions.fromString("rwx------"),
        )
    }.isSuccess
    if (!permissionSet || !dir.isDirectory || !dir.canWrite()) {
        return "flow-host:run-dir:permission"
    }
    return null
}

/** @return 当前进程 SELinux domain（失败 "unknown"）。 */
fun readSelfDomain(): String = runCatching {
    File("/proc/self/attr/current").readText()
        .trim { it == '\u0000' || it.isWhitespace() }
}.getOrDefault("unknown")

/** @throws IllegalArgumentException runId 不满足安全字符集。 */
private fun requireRunId(runId: String) {
    require(runId.matches(Regex("[A-Za-z0-9._-]{1,64}"))) {
        "flow-host:run-id:$runId"
    }
}
