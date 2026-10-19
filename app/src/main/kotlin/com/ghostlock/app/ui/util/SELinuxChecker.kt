package com.ghostlock.app.ui.util

import android.content.pm.PackageManager
import rikka.shizuku.Shizuku
import java.io.File
import java.util.concurrent.TimeUnit

/**
 *  SELinux 状态探针（2026-10-18 接线）。
 *
 *  依次尝试：
 *   1. 直接读 `/sys/fs/selinux/enforce`（0 = Permissive，1 = Enforcing）；
 *   2. 调系统自带的 `getenforce`（只读查询，不执行用户输入）；
 *   3. 反射 `android.os.SELinux.isSELinuxEnforced()`（部分 ROM 可用）；
 *   4. 都失败时：`/sys/fs/selinux` 存在 → "Unknown"，否则 "Disabled"。
 *
 *  @return "Enforcing" / "Permissive" / "Disabled" / "Unknown"。
 */
fun getSELinuxStatusRaw(): String {
    readEnforce()?.let { return it }
    runViaShizuku()?.let { return it }
    runViaRoot()?.let { return it }
    val direct = runGetenforceProbe()
    direct.status?.let { return it }
    /* 与 KernelSU 管理器同一启发式：app 域被 SELinux 拒绝读 getenforce 时，按 Enforcing 处理。
     * 依据：上游 SELinuxChecker 的 stderr.endsWith("Permission denied") → "Enforcing"。 */
    if (direct.denied) return "Enforcing"
    /* 不再用反射 SELinux.isSELinuxEnforced()：隐藏 API 在无法读 enforce 文件时会返回 false，
     * 把 Enforcing 误报成 Permissive（2026-10-18 真机实测）。 */
    return if (File("/sys/fs/selinux").exists()) "Unknown" else "Disabled"
}

private fun readEnforce(): String? = runCatching {
    val text = File("/sys/fs/selinux/enforce").readText().trim()
    when (text) {
        "1" -> "Enforcing"
        "0" -> "Permissive"
        else -> null
    }
}.getOrNull()

/** 通过 Shizuku（shell uid 2000）读 getenforce；没有 Shizuku 或未授权时返回 null。 */
private fun runViaShizuku(): String? = runCatching {
    if (!Shizuku.pingBinder()) return@runCatching null
    if (Shizuku.isPreV11()) return@runCatching null
    if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) return@runCatching null
    /* newProcess 在 Shizuku 里是 private，只能反射调（纯本地库，不是隐藏 API）。 */
    val method = Shizuku::class.java.getDeclaredMethod(
        "newProcess",
        Array<String>::class.java,
        Array<String>::class.java,
        String::class.java,
    ).apply { isAccessible = true }
    val process = method.invoke(null, arrayOf("/system/bin/getenforce"), null, null) as rikka.shizuku.ShizukuRemoteProcess
    if (!process.waitFor(2, TimeUnit.SECONDS)) {
        process.destroyForcibly()
        return@runCatching null
    }
    when (process.inputStream.bufferedReader().use { it.readText() }.trim().lowercase()) {
        "enforcing" -> "Enforcing"
        "permissive" -> "Permissive"
        "disabled" -> "Disabled"
        else -> null
    }
}.getOrNull()

/** 通过 root（KernelSU/Magisk 的 su）读 getenforce；没有授权时返回 null。 */
private fun runViaRoot(): String? = runCatching {
    val process = ProcessBuilder("su", "-c", "/system/bin/getenforce").redirectErrorStream(true).start()
    if (!process.waitFor(2, TimeUnit.SECONDS)) {
        process.destroyForcibly()
        return@runCatching null
    }
    when (process.inputStream.bufferedReader().use { it.readText() }.trim().lowercase()) {
        "enforcing" -> "Enforcing"
        "permissive" -> "Permissive"
        "disabled" -> "Disabled"
        else -> null
    }
}.getOrNull()

private data class Probe(val status: String?, val denied: Boolean)

private fun runGetenforceProbe(): Probe = runCatching {
    val process = ProcessBuilder("/system/bin/getenforce").redirectErrorStream(true).start()
    if (!process.waitFor(2, TimeUnit.SECONDS)) {
        process.destroyForcibly()
        return@runCatching Probe(null, false)
    }
    val output = process.inputStream.bufferedReader().use { it.readText() }.trim()
    when (output.lowercase()) {
        "enforcing" -> Probe("Enforcing", false)
        "permissive" -> Probe("Permissive", false)
        "disabled" -> Probe("Disabled", false)
        else -> Probe(null, output.contains("Permission denied", ignoreCase = true))
    }
}.getOrDefault(Probe(null, false))

private fun runGetenforce(): String? = runCatching {
    val process = ProcessBuilder("/system/bin/getenforce").redirectErrorStream(true).start()
    if (!process.waitFor(2, TimeUnit.SECONDS)) {
        process.destroyForcibly()
        return@runCatching null
    }
    when (process.inputStream.bufferedReader().use { it.readText() }.trim().lowercase()) {
        "enforcing" -> "Enforcing"
        "permissive" -> "Permissive"
        "disabled" -> "Disabled"
        else -> null
    }
}.getOrNull()

