package com.ghostlock.app.data.runhost.plugin

import com.ghostlock.app.core.runhost.RunHostExecutionLease
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.plugin.api.v1.DiagnosticBackendRun
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.DiagnosticRunResult

/** One-shot owner of the package classloader taint and the plugin run handle. */
internal class PluginRunResourceOwner : RunHostExecutionLease {
    private val lock = Any()
    private var packageLoader: ClassLoader? = null
    private var backendRun: DiagnosticBackendRun? = null
    private var cleanup: StageResult? = null

    @Volatile
    var pluginTainted: Boolean = false
        private set

    /** Marks the process before the first plugin class lookup. */
    fun markClassLoading(loader: ClassLoader) = synchronized(lock) {
        check(packageLoader == null) { "plugin classloader is already registered" }
        packageLoader = loader
        pluginTainted = true
    }

    /** Registers the returned run before any subsequent plugin call. */
    fun register(run: DiagnosticBackendRun) = synchronized(lock) {
        check(packageLoader != null && pluginTainted) { "plugin class loading was not marked" }
        check(backendRun == null && cleanup == null) { "plugin run is already registered or closed" }
        backendRun = run
    }

    /** Executes through the package TCCL without exposing the raw handle to other Host owners. */
    fun execute(context: DiagnosticRunContext): DiagnosticRunResult? = synchronized(lock) {
        check(cleanup == null) { "plugin run is closed" }
        val run = checkNotNull(backendRun) { "plugin run is not registered" }
        withPluginContext { run.execute(context) }
    }

    /** Closes the registered handle exactly once and retains the factual cleanup outcome. */
    fun closeAndGetCleanup(): StageResult = synchronized(lock) {
        cleanup?.let { return@synchronized it }
        val run = backendRun
        backendRun = null
        val result = if (run == null) {
            StageResult.SUCCESS
        } else {
            runCatching { withPluginContext { run.close() } }
                .fold(
                    onSuccess = { StageResult.SUCCESS },
                    onFailure = {
                        StageResult(
                            StageState.FAILED,
                            ResultReason.CLEANUP_INCOMPLETE,
                            ExitKind.NONE,
                            0,
                        )
                    },
                )
        }
        cleanup = result
        result
    }

    override fun close() {
        closeAndGetCleanup()
    }

    /** Temporarily installs the package loader and always restores the caller's TCCL. */
    fun <T> withPluginContext(block: () -> T): T {
        val loader = synchronized(lock) { checkNotNull(packageLoader) { "plugin classloader is absent" } }
        val thread = Thread.currentThread()
        val previous = thread.contextClassLoader
        thread.contextClassLoader = loader
        return try {
            block()
        } finally {
            thread.contextClassLoader = previous
        }
    }
}
