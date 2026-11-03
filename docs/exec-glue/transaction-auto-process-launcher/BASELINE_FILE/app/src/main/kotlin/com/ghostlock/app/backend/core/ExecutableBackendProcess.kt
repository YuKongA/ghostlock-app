package com.ghostlock.app.backend.core

import android.content.Context
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.FlowStepRun
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.ui.backend.BackendRunUiState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/** Runs one packaged native executable and captures its output as a Flow step result. */
internal object ExecutableBackendProcess {
    /**
     * Starts the executable from the app's native-library directory.
     *
     * @param context application context for the executable and private run directory.
     * @param backendKey stable backend identity used to partition persisted logs and files.
     * @param binaryName packaged ELF filename.
     * @param arguments exact upstream command-line arguments.
     * @param stdin optional binary input; null closes stdin without writing bytes.
     * @param files auxiliary run files keyed by simple filenames, such as the selected conf.
     * @param onLine receives each stdout/stderr line in order.
     * @return finished Flow result or a named refusal if the executable cannot start.
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
        val binary = File(context.applicationInfo.nativeLibraryDir, binaryName)
        if (!binary.isFile) {
            return@withContext BackendRunUiState.Refused(listOf("exec:binary-missing:$binaryName"))
        }
        val runDir = File(context.filesDir, "ghostlock-glue/$backendKey")
        if (!runDir.isDirectory && !runDir.mkdirs()) {
            return@withContext BackendRunUiState.Refused(listOf("exec:run-directory-unavailable"))
        }
        for ((name, bytes) in files) {
            require(name.isNotBlank() && File(name).name == name) { "exec:invalid-run-filename" }
            File(runDir, name).writeBytes(bytes)
        }

        val logFile = File(runDir, "run.log")
        val started = System.nanoTime()
        val process = try {
            ProcessBuilder(listOf(binary.absolutePath) + arguments)
                .directory(runDir)
                .redirectErrorStream(true)
                .start()
        } catch (problem: Exception) {
            return@withContext BackendRunUiState.Refused(
                listOf("exec:start-failed:" + problem::class.java.simpleName),
            )
        }
        process.outputStream.use { output -> stdin?.let(output::write) }
        logFile.bufferedWriter().use { writer ->
            process.inputStream.bufferedReader().useLines { lines ->
                lines.forEach { line ->
                    writer.appendLine(line)
                    onLine(line)
                }
            }
        }
        val exit = process.waitFor()
        val elapsed = (System.nanoTime() - started) / 1_000_000L
        val evidence = mapOf(
            "backend" to backendKey,
            "exec" to binary.absolutePath,
            "exit" to exit.toString(),
            "run_dir" to runDir.absolutePath,
        )
        val step = FlowStepRun(
            name = "exec",
            attempts = 1,
            elapsedMs = elapsed,
            result = if (exit == 0) StepResult.Done(evidence) else StepResult.Failed("exec-exit:$exit", evidence),
        )
        val result = if (exit == 0) {
            FlowResult.Ok(listOf(step), evidence)
        } else {
            FlowResult.Failed("exec-exit:$exit", "exec", listOf(step), evidence)
        }
        BackendRunUiState.Finished(result)
    }
}
