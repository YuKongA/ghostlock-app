package com.ghostlock.app.engine.ktstep

import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.IOException

/** Minimal victim process seam; close owns bounded teardown. */
interface VictimProcess : AutoCloseable {
    fun uid(): Int?
    override fun close()
}

/**
 * Kotlin victim child: a shell loop answering `id -u` per line.
 *
 * Stream creation is atomic: if either stream fails, the already-started process is torn down
 * before the constructor rethrows.
 */
class ProcessVictim(
    command: List<String> = listOf("/system/bin/sh", "-c", "while read _; do id -u; done"),
    streamFactory: (Process) -> Streams = { process ->
        Streams(process.outputStream.bufferedWriter(), process.inputStream.bufferedReader())
    },
) : VictimProcess {
    data class Streams(val input: BufferedWriter, val output: BufferedReader)

    private val process: Process
    private val input: BufferedWriter
    private val output: BufferedReader

    init {
        val started = ProcessBuilder(command).redirectErrorStream(true).start()
        try {
            val streams = streamFactory(started)
            process = started
            input = streams.input
            output = streams.output
        } catch (problem: Throwable) {
            started.destroy()
            if (!started.waitFor(500, java.util.concurrent.TimeUnit.MILLISECONDS)) started.destroyForcibly()
            throw problem
        }
    }

    @Synchronized
    override fun uid(): Int? = try {
        input.write("probe\n")
        input.flush()
        output.readLine()?.trim()?.toIntOrNull()
    } catch (failed: IOException) {
        null
    }

    override fun close() {
        runCatching { input.close() }
        runCatching { output.close() }
        process.destroy()
        try {
            if (!process.waitFor(500, java.util.concurrent.TimeUnit.MILLISECONDS)) {
                process.destroyForcibly()
                process.waitFor(500, java.util.concurrent.TimeUnit.MILLISECONDS)
            }
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }
}

/** Production-shaped victim probe: uid comes from a Kotlin child, not JNI. */
class ProcessVictimProbe(private val victim: ProcessVictim) : ProductionProbe {
    override val kind: ProbeKind = ProbeKind.Production

    override fun uid(): SyscallResult {
        val value = victim.uid() ?: return SyscallResult.Errno(KernelErrno.EPIPE)
        return SyscallResult.Ok(value.toLong())
    }
}
