package com.ghostlock.app.data.plugin

import java.io.File
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** One probe invocation's raw result: exit code plus both captured streams. */
internal data class PluginProbeOutput(
    val exitCode: Int,
    val stdout: String,
    val stderr: String,
)

/**
 * P1 injectable probe seam. The import logic only needs "run the probe and give
 * me its bytes", so tests drive it with a fake and the Android layer owns the
 * single ProcessBuilder implementation.
 */
internal fun interface PluginProbeInvoker {
    suspend fun invoke(modulePath: String, expectedSha256: String?): PluginProbeOutput
}

/**
 * Production probe runner: `libghostlock.so --plugin-probe <path>
 * [--expect-sha256 <hex>]`.
 *
 * The probe is a separate process (never in-JVM dlopen), the caller supplies
 * the expected digest so the native side hashes BEFORE dlopen, and the process
 * is killed if it outlives [timeoutMs] so a hostile module cannot hang the App.
 * stdout carries the TSV description, stderr the diagnostics: both are captured
 * separately.
 */
internal class NativePluginProbeInvoker(
    private val binary: File,
    private val workDir: File,
    private val homeDir: File,
    private val timeoutMs: Long = DEFAULT_TIMEOUT_MS,
) : PluginProbeInvoker {

    override suspend fun invoke(modulePath: String, expectedSha256: String?): PluginProbeOutput =
        withContext(Dispatchers.IO) {
            require(binary.isFile) { "missing GhostLock binary: " + binary.absolutePath }
            require(workDir.isDirectory || workDir.mkdirs()) {
                "cannot create the probe work dir: " + workDir.absolutePath
            }
            val argv = buildList {
                add(binary.absolutePath)
                add("--plugin-probe")
                add(modulePath)
                if (expectedSha256 != null) {
                    add("--expect-sha256")
                    add(expectedSha256)
                }
            }
            val process = ProcessBuilder(argv)
                .directory(workDir)
                .apply {
                    environment()["GHOSTLOCK_HOME"] = homeDir.absolutePath
                    environment()["TMPDIR"] = workDir.absolutePath
                }
                .start()
            runCatching { process.outputStream.close() }
            val stdout = StringBuilder()
            val stderr = StringBuilder()
            val errThread = Thread({
                runCatching {
                    process.errorStream.bufferedReader().forEachLine { stderr.appendLine(it) }
                }
            }, "ghostlock-plugin-probe-stderr").apply { isDaemon = true; start() }
            runCatching {
                process.inputStream.bufferedReader().forEachLine { stdout.appendLine(it) }
            }
            val finished = process.waitFor(timeoutMs, TimeUnit.MILLISECONDS)
            if (!finished) {
                process.destroyForcibly()
                runCatching { process.waitFor() }
            }
            errThread.join(ERROR_JOIN_MS)
            PluginProbeOutput(
                exitCode = if (finished) process.exitValue() else TIMEOUT_EXIT,
                stdout = stdout.toString(),
                stderr = stderr.toString(),
            )
        }

    private companion object {
        const val DEFAULT_TIMEOUT_MS = 5_000L
        const val ERROR_JOIN_MS = 200L

        /** Sentinel exit code for a probe the App had to kill. */
        const val TIMEOUT_EXIT = -1
    }
}
