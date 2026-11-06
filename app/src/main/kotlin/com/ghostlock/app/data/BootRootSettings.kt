package com.ghostlock.app.data

import android.content.Context
import android.provider.Settings
import android.util.AtomicFile
import java.io.File
import java.io.IOException

/** Stores device-local consent and an interruption marker before starting a boot run. */
internal class BootRootSettings(private val context: Context) {
    private val stateFile = AtomicFile(File(context.noBackupFilesDir, "boot-root-state"))

    @Synchronized
    fun isEnabled(): Boolean = read().enabled

    fun currentBootCount(): Int? =
        Settings.Global.getInt(context.contentResolver, Settings.Global.BOOT_COUNT, -1)
            .takeIf { it >= 0 }

    @Synchronized
    fun setEnabled(enabled: Boolean): Boolean {
        val state = read()
        if (state.enabled == enabled) return true
        val bootCount = currentBootCount()
        if (enabled && bootCount == null) return false
        return write(
            State(enabled, bootCount ?: state.bootCount, state.inProgress && bootCount == state.bootCount),
        )
    }

    @Synchronized
    fun shouldRun(bootCount: Int): Boolean {
        val state = read()
        if (!state.enabled || state.bootCount == bootCount) return false
        if (state.inProgress) {
            write(State(enabled = false, bootCount = bootCount, inProgress = false))
            return false
        }
        return true
    }

    @Synchronized
    fun begin(bootCount: Int): Boolean =
        shouldRun(bootCount) && write(State(enabled = true, bootCount = bootCount, inProgress = true))

    @Synchronized
    fun finish(bootCount: Int) {
        val state = read()
        if (state.bootCount == bootCount && state.inProgress) {
            write(state.copy(inProgress = false))
        }
    }

    private fun read(): State {
        val fields = try {
            stateFile.openRead().bufferedReader().use { it.readLine() }?.split(' ')
        } catch (_: IOException) {
            null
        }
        val enabled = fields?.getOrNull(0)?.toBooleanStrictOrNull() ?: return State.Disabled
        val bootCount = fields.getOrNull(1)?.toIntOrNull() ?: return State.Disabled
        val inProgress = fields.getOrNull(2)?.toBooleanStrictOrNull() ?: return State.Disabled
        return State(enabled, bootCount, inProgress)
    }

    private fun write(state: State): Boolean {
        val output = try {
            stateFile.startWrite()
        } catch (_: IOException) {
            return false
        }
        return try {
            output.write("${state.enabled} ${state.bootCount} ${state.inProgress}\n".toByteArray())
            // The interruption marker has to reach storage before starting native code.
            output.fd.sync()
            stateFile.finishWrite(output)
            read() == state
        } catch (_: IOException) {
            stateFile.failWrite(output)
            false
        }
    }

    private data class State(val enabled: Boolean, val bootCount: Int, val inProgress: Boolean) {
        companion object {
            val Disabled = State(enabled = false, bootCount = -1, inProgress = false)
        }
    }
}
