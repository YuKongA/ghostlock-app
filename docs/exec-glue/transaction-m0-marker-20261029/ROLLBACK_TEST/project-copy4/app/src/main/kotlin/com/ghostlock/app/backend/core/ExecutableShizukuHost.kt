package com.ghostlock.app.backend.core

import android.content.Context
import android.os.Bundle
import com.ghostlock.app.shizuku.host.ShizukuBackendHost
import java.io.File
import java.io.FileOutputStream
import java.io.RandomAccessFile
import java.util.UUID

/**
 * Owns one shell-domain upstream executable child and its per-run inputs/log.
 *
 * @param context UserService package context used only to locate the packaged native library.
 * @param backendId fixed AIDL identity.
 * @param binaryName fixed packaged ELF basename.
 * @param allowedFiles exact auxiliary file allowlist for this backend.
 * @note State, child, threads, run directory and log offset belong exclusively to this host.
 */
internal open class ExecutableShizukuHost(
    private val context: Context,
    final override val backendId: String,
    private val binaryName: String,
    private val allowedFiles: Set<String>,
) : ShizukuBackendHost {
    private var state: Int = ExecutableHostProtocol.STATE_IDLE
    private var reason: String = ""
    private var runDirectory: File? = null
    private var logFile: File? = null
    private var process: Process? = null
    private var stdinBytes: ByteArray = ByteArray(0)
    private var exitCode: Int? = null
    private var outputThread: Thread? = null
    private var inputThread: Thread? = null
    private var waitThread: Thread? = null
    private var logTruncated = false
    private var binary: File? = null
    private var arguments: List<String> = emptyList()
    private var rootScriptEvidence: StagedRunFileEvidence? = null

    @Synchronized
    override fun prepare(config: Bundle, payload: ByteArray): Int {
        if (state != ExecutableHostProtocol.STATE_IDLE) {
            reason = "exec:host:prepare-state:" + state
            return ERR_STATE
        }
        if (payload.size > MAX_INPUT_BYTES) {
            reason = "exec:host:stdin-size"
            return ERR_ARGS
        }
        val argv = config.getStringArrayList(ExecutableHostProtocol.KEY_ARGUMENTS)
            ?: return reject("exec:host:argv-missing", ERR_ARGS)
        val names = config.getStringArrayList(ExecutableHostProtocol.KEY_FILE_NAMES)
            ?: return reject("exec:host:files-missing", ERR_ARGS)
        if (argv.size > MAX_ARGS || argv.any { it.length > MAX_ARG_LENGTH || it.indexOf('\u0000') >= 0 }) {
            return reject("exec:host:argv-invalid", ERR_ARGS)
        }
        if (names.size > allowedFiles.size || names.toSet().size != names.size ||
            names.any { it !in allowedFiles || it.isBlank() || File(it).name != it }
        ) {
            return reject("exec:host:files-invalid", ERR_ARGS)
        }
        var totalBytes = payload.size.toLong()
        val files = LinkedHashMap<String, ByteArray>()
        for (name in names) {
            val bytes = config.getByteArray(ExecutableHostProtocol.fileKey(name))
                ?: return reject("exec:host:file-missing:" + name, ERR_ARGS)
            totalBytes += bytes.size
            if (totalBytes > MAX_INPUT_BYTES) return reject("exec:host:input-too-large", ERR_ARGS)
            files[name] = bytes
        }
        val stagedBinary = packagedBinary() ?: return reject("exec:host:binary-unavailable", ERR_BINARY)
        val root = File("/data/local/tmp/ghostlock-glue/" + backendId)
        val directory = File(root, UUID.randomUUID().toString())
        if (!directory.mkdirs()) return reject("exec:host:run-dir-unavailable", ERR_IO)
        directory.setReadable(false, false)
        directory.setWritable(false, false)
        directory.setExecutable(false, false)
        directory.setReadable(true, true)
        directory.setWritable(true, true)
        directory.setExecutable(true, true)
        val stagedFiles = LinkedHashMap<String, StagedRunFileEvidence>()
        try {
            files.forEach { (name, bytes) ->
                val evidence = ExecutableRunFileStager.stage(directory, name, bytes)
                stagedFiles[name] = evidence
                if (name != ExecutableRunFileStager.ROOT_SCRIPT_NAME) {
                    val target = File(directory, name)
                    target.setReadable(true, true)
                    target.setWritable(true, true)
                }
            }
        } catch (problem: ExecutableStageException) {
            runCatching { directory.deleteRecursively() }
            reason = "exec:host:" + problem.reasonCode
            return ERR_IO
        } catch (_: Exception) {
            runCatching { directory.deleteRecursively() }
            reason = "exec:host:stage-io-failed"
            return ERR_IO
        }
        val preparedArguments = try {
            prepareExecutableArguments(argv.toList())
        } catch (problem: Exception) {
            finishExecutableResources()
            runCatching { directory.deleteRecursively() }
            reason = "exec:host:prepare-arguments:" + problem::class.java.simpleName
            return ERR_ARGS
        }
        if (preparedArguments.size > MAX_ARGS || preparedArguments.any {
                it.length > MAX_ARG_LENGTH || it.indexOf('\u0000') >= 0
            }
        ) {
            finishExecutableResources()
            runCatching { directory.deleteRecursively() }
            reason = "exec:host:prepared-argv-invalid"
            return ERR_ARGS
        }
        binary = stagedBinary
        arguments = preparedArguments
        stdinBytes = payload.copyOf()
        runDirectory = directory
        rootScriptEvidence = stagedFiles[ExecutableRunFileStager.ROOT_SCRIPT_NAME]
        logFile = File(directory, "run.log")
        exitCode = null
        logTruncated = false
        reason = ""
        state = ExecutableHostProtocol.STATE_PREPARED
        return OK
    }

    @Synchronized
    override fun start(): Int {
        if (state == ExecutableHostProtocol.STATE_RUNNING) {
            return process?.let { runCatching { it.pid().toInt() }.getOrDefault(-ERR_STATE) } ?: -ERR_STATE
        }
        if (state != ExecutableHostProtocol.STATE_PREPARED) {
            return reject("exec:host:start-state:" + state, -ERR_STATE)
        }
        val executable = binary ?: return reject("exec:host:binary-missing", -ERR_BINARY)
        val directory = runDirectory ?: return reject("exec:host:run-dir-missing", -ERR_STATE)
        val command = ArrayList<String>(arguments.size + 1).apply {
            add(executable.absolutePath)
            addAll(arguments)
        }
        val child = try {
            val builder = ProcessBuilder(command)
                .directory(directory)
                .redirectErrorStream(true)
            val backendKey = if (binaryName == "lib43499-Glued.so") "43499-Glued" else "43284Glued"
            builder.environment().putAll(ExecutableRunEnvironment.variables(backendKey, directory))
            builder.start()
        } catch (problem: Exception) {
            command.clear()
            arguments = emptyList()
            state = ExecutableHostProtocol.STATE_UNKNOWN
            reason = "exec:host:start:" + problem::class.java.simpleName
            return -ERR_SPAWN
        }
        command.clear()
        arguments = emptyList()
        process = child
        state = ExecutableHostProtocol.STATE_RUNNING
        outputThread = Thread({ drain(child) }, "glued-shell-exec-log").apply {
            isDaemon = true
            start()
        }
        inputThread = Thread({ writeStdin(child) }, "glued-shell-exec-stdin").apply {
            isDaemon = true
            start()
        }
        waitThread = Thread({ reap(child) }, "glued-shell-exec-wait").apply {
            isDaemon = true
            start()
        }
        return runCatching { child.pid().toInt() }.getOrElse {
            reason = "exec:host:pid-unavailable"
            -ERR_STATE
        }
    }

    @Synchronized
    override fun state(): Int = state

    @Synchronized
    override fun readLog(offset: Long, maxBytes: Int): ByteArray {
        if (offset < 0 || maxBytes <= 0 || maxBytes > MAX_LOG_CHUNK) return ByteArray(0)
        val file = logFile ?: return ByteArray(0)
        if (!file.isFile) return ByteArray(0)
        return runCatching {
            RandomAccessFile(file, "r").use { input ->
                if (offset >= input.length()) return@use ByteArray(0)
                val size = minOf((input.length() - offset).toInt(), maxBytes)
                ByteArray(size).also { bytes ->
                    input.seek(offset)
                    input.readFully(bytes)
                }
            }
        }.getOrDefault(ByteArray(0))
    }

    @Synchronized
    override fun verify(): ByteArray = buildList {
        add("state=" + state)
        add("exit=" + (exitCode?.toString() ?: "unknown"))
        add("executable=" + (binary?.absolutePath ?: ""))
        add("run_dir=" + (runDirectory?.absolutePath ?: ""))
        add("log_truncated=" + if (logTruncated) "1" else "0")
        rootScriptEvidence?.fields("root_script")?.forEach { (key, value) -> add("$key=$value") }
    }.joinToString("\n").toByteArray(Charsets.UTF_8)

    @Synchronized
    override fun lastReason(): String = reason

    override fun bootId(): String = runCatching {
        File("/proc/sys/kernel/random/boot_id").readText().trim().take(128)
    }.getOrDefault("")

    override fun attachLogFd(rawFd: Int): Boolean {
        /* false leaves raw fd ownership with GhostlockUserService's generic adapter. */
        return false
    }

    override fun logLine(level: Int, line: String) = Unit

    /** Stops the direct child and removes only a run which never crossed the start boundary.
     *
     * @note The PREPARED-to-UNKNOWN transition is atomic with child capture so a concurrent
     * start either wins first and is terminated here, or observes UNKNOWN and is rejected.
     */
    override fun destroy() {
        val (child, preparedRunDirectory) = synchronized(this) {
            val currentChild = process
            val preparedDirectory = runDirectory.takeIf {
                currentChild == null && state == ExecutableHostProtocol.STATE_PREPARED
            }
            if (preparedDirectory != null) state = ExecutableHostProtocol.STATE_UNKNOWN
            currentChild to preparedDirectory
        }
        if (child != null && child.isAlive) {
            runCatching { child.destroy() }
            runCatching {
                if (!child.waitFor(DESTROY_WAIT_MS, java.util.concurrent.TimeUnit.MILLISECONDS)) {
                    child.destroyForcibly()
                    child.waitFor(DESTROY_WAIT_MS, java.util.concurrent.TimeUnit.MILLISECONDS)
                }
            }
        }
        listOf(outputThread, inputThread, waitThread).forEach { thread ->
            if (thread != null && thread !== Thread.currentThread()) {
                runCatching { thread.join(THREAD_JOIN_MS) }
            }
        }
        runCatching { child?.inputStream?.close() }
        runCatching { child?.outputStream?.close() }
        runCatching { child?.errorStream?.close() }
        finishExecutableResources()
        if (preparedRunDirectory != null) {
            runCatching { preparedRunDirectory.deleteRecursively() }
        }
        synchronized(this) {
            if (child?.isAlive != true) process = null
            if (preparedRunDirectory != null && runDirectory == preparedRunDirectory) {
                runDirectory = null
                logFile = null
                rootScriptEvidence = null
            }
            state = if (exitCode != null) ExecutableHostProtocol.STATE_TERMINAL
            else ExecutableHostProtocol.STATE_UNKNOWN
        }
    }

    private fun packagedBinary(): File? {
        val libraryDirectory = File(context.applicationInfo.nativeLibraryDir).canonicalFile
        val candidate = File(libraryDirectory, binaryName).canonicalFile
        if (candidate.parentFile != libraryDirectory || !candidate.isFile || !candidate.canExecute()) return null
        return candidate
    }

    private fun writeStdin(child: Process) {
        try {
            child.outputStream.use { output -> output.write(stdinBytes) }
        } catch (problem: Exception) {
            synchronized(this) {
                if (reason.isBlank()) reason = "exec:host:stdin:" + problem::class.java.simpleName
            }
        } finally {
            stdinBytes.fill(0)
        }
    }

    private fun drain(child: Process) {
        try {
            child.inputStream.bufferedReader().useLines { lines ->
                lines.forEach(::appendLogLine)
            }
        } catch (problem: Exception) {
            synchronized(this) {
                if (reason.isBlank()) reason = "exec:host:log:" + problem::class.java.simpleName
            }
        }
    }

    private fun appendLogLine(line: String) {
        val file = logFile ?: return
        val bytes = (line.take(MAX_LINE_CHARS) + "\n").toByteArray(Charsets.UTF_8)
        synchronized(this) {
            val currentSize = file.length()
            if (currentSize + bytes.size <= MAX_LOG_BYTES) {
                runCatching { FileOutputStream(file, true).use { it.write(bytes) } }
            } else if (!logTruncated) {
                logTruncated = true
                val marker = "GHOSTLOCK_LOG_TRUNCATED\n".toByteArray(Charsets.UTF_8)
                if (currentSize + marker.size <= MAX_LOG_BYTES) {
                    runCatching { FileOutputStream(file, true).use { it.write(marker) } }
                }
            }
        }
    }

    private fun reap(child: Process) {
        try {
            val exit = child.waitFor()
            inputThread?.join(THREAD_JOIN_MS)
            outputThread?.join(THREAD_JOIN_MS)
            finishExecutableResources()
            val scriptIntegrityFailure = runCatching {
                val expected = rootScriptEvidence
                if (expected != null) {
                    val directory = runDirectory ?: throw ExecutableStageException("stage:run-dir-missing")
                    ExecutableRunFileStager.requireUnchanged(directory, expected)
                }
            }.exceptionOrNull()
            synchronized(this) {
                exitCode = exit
                if (scriptIntegrityFailure != null) {
                    reason = "exec:host:root-script-identity-changed"
                } else if (exit != 0 && reason.isBlank()) {
                    reason = "exec-exit:" + exit
                }
                state = ExecutableHostProtocol.STATE_TERMINAL
            }
        } catch (problem: Exception) {
            synchronized(this) {
                reason = "exec:host:wait:" + problem::class.java.simpleName
                state = ExecutableHostProtocol.STATE_UNKNOWN
            }
        }
    }

    private fun reject(message: String, code: Int): Int {
        reason = message
        return code
    }

    /**
     * Prepares launcher-domain resources and the final structured argv after input validation.
     *
     * @param arguments validated caller-supplied arguments; backend adapters may require empty.
     * @return process arguments after launcher-owned runtime parameters are appended.
     * @throws Exception when the selected host-domain setup cannot be prepared.
     * @note Implementations that acquire resources must release them from
     * [finishExecutableRun], including prepare failure and destroy paths.
     */
    protected open fun prepareExecutableArguments(arguments: List<String>): List<String> = arguments

    /**
     * Releases resources acquired by [prepareExecutableArguments] after child exit or teardown.
     *
     * @throws Exception when an owned resource fails to close; the host preserves a named reason.
     */
    protected open fun finishExecutableRun() = Unit

    /**
     * Runs the adapter cleanup hook and preserves a non-secret failure class in host state.
     *
     * @note Called after normal child exit, explicit destroy, and failed runtime preparation.
     */
    private fun finishExecutableResources() {
        try {
            finishExecutableRun()
        } catch (problem: Exception) {
            synchronized(this) {
                if (reason.isBlank()) reason = "exec:host:resource-close:" + problem::class.java.simpleName
            }
        }
    }

    private companion object {
        private const val OK = 0
        private const val ERR_ARGS = 1
        private const val ERR_BINARY = 2
        private const val ERR_STATE = 7
        private const val ERR_IO = 8
        private const val ERR_SPAWN = 6
        private const val MAX_ARGS = 32
        private const val MAX_ARG_LENGTH = 4096
        private const val MAX_INPUT_BYTES = 512 * 1024
        private const val MAX_LOG_BYTES = 8 * 1024 * 1024
        private const val MAX_LOG_CHUNK = 32 * 1024
        private const val MAX_LINE_CHARS = 8192
        private const val DESTROY_WAIT_MS = 1000L
        private const val THREAD_JOIN_MS = 1000L
    }
}
