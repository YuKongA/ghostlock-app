package com.ghostlock.app.data.device

import android.content.pm.PackageManager
import java.io.File
import java.util.concurrent.TimeUnit
import rikka.shizuku.Shizuku

/**
 * Reads SELinux status through the existing direct, Shizuku, root, and getenforce sequence.
 *
 * @note Probe failure returns `Unknown` when selinuxfs exists and `Disabled` otherwise.
 */
class SELinuxStatusReader {
    /**
     * Runs the ordered read-only status probes.
     *
     * @return Enforcing, Permissive, Disabled, or Unknown.
     */
    fun read(): String {
        readEnforce()?.let { return it }
        runViaShizuku()?.let { return it }
        runViaRoot()?.let { return it }
        val direct = runGetenforceProbe()
        direct.status?.let { return it }
        if (direct.denied) return STATUS_ENFORCING
        return if (File(SELINUX_DIRECTORY).exists()) STATUS_UNKNOWN else STATUS_DISABLED
    }

    /** @return Direct selinuxfs status, or null when the file cannot provide a known value. */
    private fun readEnforce(): String? = runCatching {
        when (File(ENFORCE_PATH).readText().trim()) {
            "1" -> STATUS_ENFORCING
            "0" -> STATUS_PERMISSIVE
            else -> null
        }
    }.getOrNull()

    /** @return Shizuku getenforce status, or null when unavailable, unauthorized, or unknown. */
    private fun runViaShizuku(): String? = runCatching {
        if (!Shizuku.pingBinder()) return@runCatching null
        if (Shizuku.isPreV11()) return@runCatching null
        if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) return@runCatching null
        val method = Shizuku::class.java.getDeclaredMethod(
            "newProcess",
            Array<String>::class.java,
            Array<String>::class.java,
            String::class.java,
        ).apply { isAccessible = true }
        val process = method.invoke(
            null,
            arrayOf(GETENFORCE_PATH),
            null,
            null,
        ) as rikka.shizuku.ShizukuRemoteProcess
        if (!process.waitFor(PROBE_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            return@runCatching null
        }
        normalize(process.inputStream.bufferedReader().use { it.readText() })
    }.getOrNull()

    /** @return Root getenforce status, or null when su/probe is unavailable or unknown. */
    private fun runViaRoot(): String? = runCatching {
        val process = ProcessBuilder("su", "-c", GETENFORCE_PATH).redirectErrorStream(true).start()
        if (!process.waitFor(PROBE_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            return@runCatching null
        }
        normalize(process.inputStream.bufferedReader().use { it.readText() })
    }.getOrNull()

    /** @return Direct getenforce result plus whether execution was denied by SELinux. */
    private fun runGetenforceProbe(): Probe = runCatching {
        val process = ProcessBuilder(GETENFORCE_PATH).redirectErrorStream(true).start()
        if (!process.waitFor(PROBE_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            return@runCatching Probe(null, false)
        }
        val output = process.inputStream.bufferedReader().use { it.readText() }.trim()
        Probe(
            status = normalize(output),
            denied = output.contains("Permission denied", ignoreCase = true),
        )
    }.getOrDefault(Probe(null, false))

    /** @return Canonical status for one getenforce output, or null for an unknown value. */
    private fun normalize(output: String): String? = when (output.trim().lowercase()) {
        "enforcing" -> STATUS_ENFORCING
        "permissive" -> STATUS_PERMISSIVE
        "disabled" -> STATUS_DISABLED
        else -> null
    }

    private data class Probe(val status: String?, val denied: Boolean)

    private companion object {
        const val ENFORCE_PATH = "/sys/fs/selinux/enforce"
        const val SELINUX_DIRECTORY = "/sys/fs/selinux"
        const val GETENFORCE_PATH = "/system/bin/getenforce"
        const val PROBE_TIMEOUT_SECONDS = 2L
        const val STATUS_ENFORCING = "Enforcing"
        const val STATUS_PERMISSIVE = "Permissive"
        const val STATUS_DISABLED = "Disabled"
        const val STATUS_UNKNOWN = "Unknown"
    }
}
