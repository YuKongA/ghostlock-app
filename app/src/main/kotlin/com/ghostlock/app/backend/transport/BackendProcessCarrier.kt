package com.ghostlock.app.backend.transport

import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.LinkOption
import java.util.concurrent.TimeUnit

/**
 * Generic executable-carrier launcher without Flow reflection or product-result inference.
 *
 * @property executable Approved host-local ET_EXEC/helper file.
 * @property executableIdentity Identity captured when the host published the executable.
 * @property arguments Direct argv excluding [executable].
 * @property environment Explicit environment additions.
 * @property workingDirectory Host-owned working directory.
 * @property timeoutMs Positive timeout, or zero for no timeout.
 */
data class BackendProcessRequest(
    val executable: File,
    val executableIdentity: PublishedArtifactIdentity,
    val arguments: List<String>,
    val environment: Map<String, String> = emptyMap(),
    val workingDirectory: File,
    val timeoutMs: Long = 0L,
)

/** Typed process observation retained only as a backend stage fact. */
data class BackendProcessResult(
    val stage: StageResult,
    val stdout: String,
    val stderr: String,
)

/** Launches and observes one independent backend executable carrier. */
class BackendProcessCarrier {
    /**
     * Runs one approved process and never interprets output text as product success.
     *
     * @param request Direct process request.
     * @param cancellation Safe cancellation view; callers must invoke only under the backend abort policy.
     * @return Typed exit fact plus bounded diagnostic output.
     */
    fun run(
        request: BackendProcessRequest,
        cancellation: RunCancellationView,
    ): BackendProcessResult {
        require(Files.isRegularFile(request.executable.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            "carrier executable is not a regular file"
        }
        require(!Files.isSymbolicLink(request.executable.toPath()) && request.executable.canExecute()) {
            "carrier executable is not an executable ordinary file"
        }
        require(request.executableIdentity.matches(request.executable)) { "carrier executable identity changed" }
        require(
            Files.isDirectory(request.workingDirectory.toPath(), LinkOption.NOFOLLOW_LINKS) &&
                !Files.isSymbolicLink(request.workingDirectory.toPath()),
        ) { "carrier working directory is missing or linked" }
        require(request.executable.canonicalFile.parentFile == request.workingDirectory.canonicalFile) {
            "carrier executable is outside its host-owned working directory"
        }
        require(request.arguments.size <= MAX_ARGUMENTS) { "too many carrier arguments" }
        require(request.timeoutMs >= 0L) { "negative carrier timeout" }
        require(request.environment.keys.all(ALLOWED_ENVIRONMENT_KEYS::contains)) {
            "carrier environment contains a configuration side channel"
        }
        val builder = ProcessBuilder(listOf(request.executable.absolutePath) + request.arguments)
            .directory(request.workingDirectory)
        builder.environment().putAll(request.environment)
        val process = runCatching { builder.start() }.getOrElse {
            return BackendProcessResult(
                StageResult(StageState.FAILED, ResultReason.IO_FAILURE, ExitKind.NONE, 0),
                "",
                "",
            )
        }
        val stdoutCollector = BoundedOutputCollector(process.inputStream)
        val stderrCollector = BoundedOutputCollector(process.errorStream)
        val stdoutThread = stdoutCollector.start("ghostlock-carrier-stdout")
        val stderrThread = stderrCollector.start("ghostlock-carrier-stderr")
        val startedAt = System.nanoTime()
        var timedOut = false
        var cancelled = false
        while (process.isAlive) {
            if (cancellation.isCancellationRequested()) {
                cancelled = true
                process.destroy()
                break
            }
            if (request.timeoutMs > 0L && elapsedMs(startedAt) >= request.timeoutMs) {
                timedOut = true
                process.destroy()
                break
            }
            process.waitFor(POLL_MS, TimeUnit.MILLISECONDS)
        }
        if (process.isAlive) process.destroyForcibly()
        process.waitFor()
        stdoutThread.join()
        stderrThread.join()
        val stdout = stdoutCollector.text()
        val stderr = stderrCollector.text()
        val stage = when {
            cancelled -> StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0)
            timedOut -> StageResult(StageState.FAILED, ResultReason.TIMEOUT, ExitKind.NONE, 0)
            !process.isAlive && process.exitValue() == 0 -> StageResult(
                StageState.SUCCESS,
                ResultReason.NONE,
                ExitKind.PROCESS_EXIT,
                0,
            )

            !process.isAlive -> StageResult(
                StageState.FAILED,
                ResultReason.PROCESS_EXIT_NONZERO,
                ExitKind.PROCESS_EXIT,
                process.exitValue().coerceIn(1, 255),
            )

            else -> StageResult(StageState.UNKNOWN, ResultReason.INTERNAL_CONTRACT_FAILURE, ExitKind.NONE, 0)
        }
        return BackendProcessResult(stage, stdout, stderr)
    }

    /** Returns non-negative elapsed milliseconds. */
    private fun elapsedMs(startedAt: Long): Long = (System.nanoTime() - startedAt).coerceAtLeast(0L) / 1_000_000L

    private companion object {
        const val MAX_ARGUMENTS: Int = 64
        const val MAX_OUTPUT_CHARS: Int = 64 * 1024
        const val POLL_MS: Long = 100L
        val ALLOWED_ENVIRONMENT_KEYS: Set<String> = setOf(
            "GHOSTLOCK_HOME",
            "TMPDIR",
            "GHOSTLOCK_KSU_LOG",
        )
    }

    /** Drains one process pipe fully while retaining only a bounded diagnostic prefix. */
    private class BoundedOutputCollector(
        private val input: InputStream,
    ) {
        private val retained = StringBuilder()

        /** Starts the sole collector thread owned by one carrier invocation. */
        fun start(name: String): Thread = Thread(
            {
                input.bufferedReader().use { reader ->
                    val buffer = CharArray(4096)
                    while (true) {
                        val count = reader.read(buffer)
                        if (count < 0) break
                        val remaining = MAX_OUTPUT_CHARS - retained.length
                        if (remaining > 0) retained.append(buffer, 0, minOf(count, remaining))
                    }
                }
            },
            name,
        ).apply {
            isDaemon = true
            start()
        }

        /** Returns the retained bounded prefix after the owner joined the collector thread. */
        fun text(): String = retained.toString()
    }
}
