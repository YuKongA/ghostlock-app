package com.ghostlock.app.backend.core

import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import com.ghostlock.app.backend.cve43499glued.Glued43499ExecutableHost
import com.ghostlock.app.backend.cve43284glued.AndroidIpSecRuntime
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.FlowStepRun
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.shizuku.ShizukuBackendClient
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts
import com.ghostlock.app.ui.backend.BackendRunUiState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import rikka.shizuku.Shizuku
import java.io.File

/** Route selected before any child is created; an execution failure never changes this value. */
internal enum class ExecutableProcessRoute { APP, SHIZUKU }

/** Pure route rule shared by production dispatch and JVM tests. */
internal object ExecutableProcessRouting {
    /**
     * Chooses Shizuku only when its binder is reachable and permission is granted.
     *
     * @param binderReachable result of Shizuku.pingBinder().
     * @param permissionGranted result of Shizuku.checkSelfPermission().
     * @return the single owner that may create this run's child.
     */
    fun select(binderReachable: Boolean, permissionGranted: Boolean): ExecutableProcessRoute =
        if (binderReachable && permissionGranted) ExecutableProcessRoute.SHIZUKU
        else ExecutableProcessRoute.APP
}

/** Runs one packaged upstream executable through exactly one selected process launcher. */
internal object ExecutableBackendProcess {
    private const val MAX_INPUT_BYTES = 512 * 1024
    private const val MAX_LOG_BYTES = 8 * 1024 * 1024
    private const val MAX_LOG_LINE_CHARS = 8192

    /**
     * Chooses one launcher before process creation; an authorized Shizuku failure is returned
     * as-is and never starts a second app child.
     *
     * @param context application context for executable and private run directory.
     * @param backendKey stable backend identity.
     * @param binaryName packaged ELF filename; it must match the fixed backend mapping.
     * @param arguments exact upstream arguments, never a shell command string.
     * @param stdin optional bytes written to the child's stdin.
     * @param files auxiliary files keyed by simple filenames.
     * @param onLine receives output lines.
     * @return terminal Flow state or a named pre-start refusal.
     */
    suspend fun run(
        context: Context,
        backendKey: String,
        binaryName: String,
        arguments: List<String>,
        stdin: ByteArray? = null,
        files: Map<String, ByteArray> = emptyMap(),
        onLine: (String) -> Unit = {},
    ): BackendRunUiState = withContext(Dispatchers.IO) {
        val validation = validateRequest(backendKey, binaryName, arguments, stdin, files)
        if (validation != null) return@withContext BackendRunUiState.Refused(listOf(validation))
        val route = ExecutableProcessRouting.select(
            binderReachable = runCatching { Shizuku.pingBinder() }.getOrDefault(false),
            permissionGranted = runCatching {
                Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
            }.getOrDefault(false),
        )
        when (route) {
            ExecutableProcessRoute.APP -> runApp(
                context.applicationContext, backendKey, binaryName, arguments, stdin, files, onLine,
            )
            ExecutableProcessRoute.SHIZUKU -> runShizuku(
                context.applicationContext, backendKey, arguments, stdin, files, onLine,
            )
        }
    }

    /**
     * Validates fixed backend identity, argv ownership, filenames, and the bounded input budget.
     *
     * @param backendKey stable Glued backend identity.
     * @param binaryName caller's packaged ELF basename.
     * @param arguments structured argv supplied by the backend Flow.
     * @param stdin optional bytes written to the process standard input.
     * @param files auxiliary bytes keyed by allowlisted simple filenames.
     * @return null for valid input, otherwise a stable fail-closed reason.
     * @note 43284 argv is generated only inside the selected launcher runtime.
     */
    internal fun validateRequest(
        backendKey: String,
        binaryName: String,
        arguments: List<String>,
        stdin: ByteArray?,
        files: Map<String, ByteArray>,
    ): String? {
        val expectedBinary = when (backendKey) {
            "43499-Glued" -> "lib43499-Glued.so"
            "43284Glued" -> "lib43284Glued.so"
            else -> return "exec:backend-unsupported:$backendKey"
        }
        if (binaryName != expectedBinary) return "exec:binary-identity-mismatch"
        if (backendKey == "43284Glued" && arguments.isNotEmpty()) {
            return "exec:43284:caller-arguments-forbidden"
        }
        if (arguments.size > 32 || arguments.any { it.indexOf('\u0000') >= 0 || it.length > 4096 }) {
            return "exec:arguments-invalid"
        }
        val allowedFiles = when (backendKey) {
            "43499-Glued" -> setOf("profile.conf", "profile.bin", "finish.sh", "payload")
            "43284Glued" -> setOf("finish.sh", "payload")
            else -> emptySet()
        }
        if (files.keys.any { it !in allowedFiles }) return "exec:file-name-invalid"
        if (files.values.any { it.size > MAX_INPUT_BYTES }) return "exec:file-size-invalid"
        val total = (stdin?.size ?: 0).toLong() + files.values.sumOf { it.size.toLong() }
        if (total > MAX_INPUT_BYTES) return "exec:input-too-large"
        return null
    }

    /**
     * Executes the fixed ELF in App UID and owns that process, streams, run directory and log.
     *
     * @param context application context used to locate the ELF and private run directory.
     * @param backendKey fixed Glued backend identity.
     * @param binaryName allowlisted packaged ELF name.
     * @param arguments structured non-secret backend argv; 43284 values are generated below.
     * @param stdin optional bounded standard-input bytes.
     * @param files validated auxiliary run files.
     * @param onLine output sink for the bounded local log.
     * @return terminal process result or a named setup/start refusal.
     * @throws CancellationException after terminating/reaping the direct child on cancellation.
     * @note The 43284 runtime remains open until the foreground ELF returns.
     */
    private suspend fun runApp(
        context: Context,
        backendKey: String,
        binaryName: String,
        arguments: List<String>,
        stdin: ByteArray?,
        files: Map<String, ByteArray>,
        onLine: (String) -> Unit,
    ): BackendRunUiState {
        val binary = File(context.applicationInfo.nativeLibraryDir, binaryName)
        if (!binary.isFile || !binary.canExecute()) {
            return BackendRunUiState.Refused(listOf("exec:binary-missing:$binaryName"))
        }
        val runDir = File(context.filesDir, "ghostlock-glue/$backendKey")
        if (!runDir.isDirectory && !runDir.mkdirs()) {
            return BackendRunUiState.Refused(listOf("exec:run-directory-unavailable"))
        }
        for ((name, bytes) in files) File(runDir, name).writeBytes(bytes)
        if (backendKey != "43284Glued") {
            return runProcess(
                command = processCommand(binary.absolutePath, arguments),
                runDir = runDir,
                backendKey = backendKey,
                displayExecutable = binary.absolutePath,
                stdin = stdin,
                onLine = onLine,
            )
        }

        val runtime = try {
            AndroidIpSecRuntime.create(context)
        } catch (problem: Exception) {
            return BackendRunUiState.Refused(
                listOf("exec:ipsec:setup:${problem::class.java.simpleName}"),
            )
        }
        var closeFailure: Throwable? = null
        val result = try {
            runProcess(
                command = processCommand(binary.absolutePath, arguments + runtime.arguments),
                runDir = runDir,
                backendKey = backendKey,
                displayExecutable = binary.absolutePath,
                stdin = stdin,
                onLine = onLine,
            )
        } finally {
            closeFailure = runCatching { runtime.close() }.exceptionOrNull()
        }
        if (closeFailure != null) {
            return BackendRunUiState.Refused(listOf("exec:ipsec:cleanup-failed"))
        }
        return result
    }

    /** Shizuku UserService launcher; the host receives only fixed IDs and bounded structured data. */
    private suspend fun runShizuku(
        context: Context,
        backendKey: String,
        arguments: List<String>,
        stdin: ByteArray?,
        files: Map<String, ByteArray>,
        onLine: (String) -> Unit,
    ): BackendRunUiState {
        val backendId = when (backendKey) {
            "43499-Glued" -> ShizukuBackendHosts.CVE_43499_GLUED
            "43284Glued" -> ShizukuBackendHosts.CVE_43284_GLUED
            else -> return BackendRunUiState.Refused(listOf("exec:backend-unsupported:$backendKey"))
        }
        val client = ShizukuBackendClient(context, backendId)
        val config = Bundle().apply {
            putStringArrayList(Glued43499ExecutableHost.KEY_ARGUMENTS, ArrayList(arguments))
            putStringArrayList(Glued43499ExecutableHost.KEY_FILE_NAMES, ArrayList(files.keys))
            files.forEach { (name, bytes) -> putByteArray(Glued43499ExecutableHost.fileKey(name), bytes) }
        }
        val startedAt = System.nanoTime()
        var terminal = false
        try {
            val unavailable = client.availabilityReasons()
            if (unavailable.isNotEmpty()) {
                return BackendRunUiState.Refused(unavailable)
            }
            val prepared = client.prepare(config, stdin ?: ByteArray(0), onLine)
            if (prepared != 0) {
                val reason = client.lastReason()
                return BackendRunUiState.Refused(
                    listOf(reason.ifBlank { "exec:shizuku:prepare:$prepared" }),
                )
            }
            val pid = client.start()
            if (pid <= 0) {
                val reason = client.lastReason()
                return BackendRunUiState.Refused(
                    listOf(reason.ifBlank { "exec:shizuku:start:$pid" }),
                )
            }
            while (true) {
                val state = client.state()
                if (state == Glued43499ExecutableHost.STATE_TERMINAL) {
                    terminal = true
                    val hostReason = client.lastReason()
                    if (hostReason.startsWith("exec:host:resource-close:")) {
                        return BackendRunUiState.Refused(listOf(hostReason))
                    }
                    val facts = client.verify().toString(Charsets.UTF_8).lineSequence()
                        .mapNotNull { line ->
                            line.split('=', limit = 2).takeIf { it.size == 2 }?.let { it[0] to it[1] }
                        }.toMap()
                    val exit = facts["exit"]?.toIntOrNull()
                        ?: return BackendRunUiState.Refused(listOf("exec:shizuku:exit-unknown"))
                    return finished(
                        backendKey = backendKey,
                        executable = facts["executable"] ?: "shizuku:$backendId",
                        exit = exit,
                        elapsedMs = (System.nanoTime() - startedAt) / 1_000_000L,
                        runDir = facts["run_dir"] ?: "shizuku:$backendId",
                    )
                }
                if (state != Glued43499ExecutableHost.STATE_RUNNING) {
                    val reason = client.lastReason()
                    return BackendRunUiState.Refused(
                        listOf(reason.ifBlank { "exec:shizuku:state:$state" }),
                    )
                }
                delay(100)
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Exception) {
            return BackendRunUiState.Refused(
                listOf("exec:shizuku:failed:" + problem::class.java.simpleName),
            )
        } finally {
            withContext(NonCancellable + Dispatchers.IO) {
                /* Stop/reap any uncertain child before removing the shared host service. */
                client.disconnect(remove = true)
                if (!terminal) onLine("exec:shizuku:launcher-ended-without-terminal-result")
            }
        }
    }

    /**
     * Starts one direct ProcessBuilder child and concurrently drains output and writes stdin.
     *
     * @param command mutable owner of the fixed argv list; cleared immediately after start.
     * @param runDir backend-specific working and log directory.
     * @param backendKey stable identity included in terminal evidence.
     * @param displayExecutable safe executable identifier for terminal facts.
     * @param stdin optional bytes written once, then the input stream is closed.
     * @param onLine receives each drained output line.
     * @return terminal process result or a named spawn/process refusal.
     * @throws CancellationException after forcibly stopping and waiting for the direct child.
     * @note This method never constructs a shell command string.
     */
    private suspend fun runProcess(
        command: ArrayList<String>,
        runDir: File,
        backendKey: String,
        displayExecutable: String,
        stdin: ByteArray?,
        onLine: (String) -> Unit,
    ): BackendRunUiState = withContext(Dispatchers.IO) {
        val logFile = File(runDir, "run.log")
        runCatching { logFile.writeBytes(ByteArray(0)) }
        val startedAt = System.nanoTime()
        val process = try {
            ProcessBuilder(command).directory(runDir).redirectErrorStream(true).start()
        } catch (problem: Exception) {
            command.clear()
            return@withContext BackendRunUiState.Refused(
                listOf("exec:start-failed:" + problem::class.java.simpleName),
            )
        }
        command.clear()
        val reader = Thread({
            val logState = LocalLogState()
            process.inputStream.bufferedReader().useLines { lines ->
                lines.forEach { line ->
                    appendLocalLog(logFile, logState, line)
                    onLine(line)
                }
            }
        }, "glued-exec-log").apply { isDaemon = true; start() }
        try {
            process.outputStream.use { output -> stdin?.let(output::write) }
            val exit = process.waitFor()
            reader.join()
            val elapsed = (System.nanoTime() - startedAt) / 1_000_000L
            finished(backendKey, displayExecutable, exit, elapsed, runDir.absolutePath)
        } catch (cancelled: CancellationException) {
            process.destroyForcibly()
            runCatching { process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS) }
            runCatching { reader.join(1000) }
            throw cancelled
        } catch (problem: Exception) {
            process.destroyForcibly()
            runCatching { process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS) }
            runCatching { reader.join(1000) }
            BackendRunUiState.Refused(listOf("exec:process-failed:" + problem::class.java.simpleName))
        }
    }

    /** App-side bounded log writer; child output continues draining after the retained cap. */
    private fun appendLocalLog(file: File, state: LocalLogState, line: String) {
        val bytes = (line.take(MAX_LOG_LINE_CHARS) + "\n").toByteArray(Charsets.UTF_8)
        synchronized(state) {
            if (state.bytes + bytes.size <= MAX_LOG_BYTES) {
                runCatching { file.appendBytes(bytes) }
                state.bytes += bytes.size
            } else if (!state.truncated) {
                state.truncated = true
                val marker = "GHOSTLOCK_LOG_TRUNCATED\n".toByteArray(Charsets.UTF_8)
                if (state.bytes + marker.size <= MAX_LOG_BYTES) {
                    runCatching { file.appendBytes(marker) }
                    state.bytes += marker.size
                }
            }
        }
    }

    /**
     * Builds one direct-exec argv owner and avoids shell parsing or a persisted command string.
     *
     * @param executable fixed packaged ELF path validated for this backend.
     * @param arguments structured upstream argv tokens.
     * @return mutable argv owner; [runProcess] clears it immediately after spawn.
     * @note Used only during the executable launch phase.
     */
    private fun processCommand(executable: String, arguments: List<String>): ArrayList<String> =
        ArrayList<String>(arguments.size + 1).apply {
            add(executable)
            addAll(arguments)
        }

    /** Mutable accounting scoped to the one app-launched process and output-drain thread. */
    private class LocalLogState {
        var bytes: Long = 0L
        var truncated: Boolean = false
    }

    /** Converts one upstream process exit into the app Flow result shape. */
    private fun finished(
        backendKey: String,
        executable: String,
        exit: Int,
        elapsedMs: Long,
        runDir: String,
    ): BackendRunUiState {
        val evidence = mapOf(
            "backend" to backendKey,
            "exec" to executable,
            "exit" to exit.toString(),
            "run_dir" to runDir,
        )
        val step = FlowStepRun(
            name = "exec",
            attempts = 1,
            elapsedMs = elapsedMs,
            result = if (exit == 0) StepResult.Done(evidence) else StepResult.Failed("exec-exit:$exit", evidence),
        )
        val result = if (exit == 0) FlowResult.Ok(listOf(step), evidence)
        else FlowResult.Failed("exec-exit:$exit", "exec", listOf(step), evidence)
        return BackendRunUiState.Finished(result)
    }
}
