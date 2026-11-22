package com.ghostlock.app.composition.plugin

import com.ghostlock.app.composition.backend.BackendCatalogProjection
import com.ghostlock.app.composition.backend.BackendCatalogView
import com.ghostlock.app.core.plugin.PluginCatalogState
import com.ghostlock.app.data.plugin.BuiltinPluginSeeder
import java.util.concurrent.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** Application-scoped, one-shot publisher for the installed plugin catalog. */
internal class PluginCatalogCoordinator(
    private val seeder: BuiltinPluginSeeder,
    private val sdkInt: Int,
    private val processIs64Bit: Boolean,
    private val machine: String,
) {
    private val initialization = Mutex()
    private val mutableState = MutableStateFlow<PluginCatalogState>(PluginCatalogState.Initializing)
    private var publishedView: BackendCatalogView? = null
    private var publishedDefaultBackendKey: String? = null

    val state: StateFlow<PluginCatalogState> = mutableState.asStateFlow()

    /** Installs/revalidates builtin objects and publishes exactly one terminal process generation. */
    suspend fun initialize(): PluginCatalogState = initialization.withLock {
        if (mutableState.value !is PluginCatalogState.Initializing) return mutableState.value
        try {
            val seed = withContext(Dispatchers.IO) { seeder.reconcile() }
            val view = BackendCatalogProjection.project(
                seed.snapshot,
                sdkInt = sdkInt,
                processIs64Bit = processIs64Bit,
                machine = machine,
            )
            require(view.resolve(seed.defaultBackendKey) != null) {
                "builtin default backend is absent from the READY catalog projection"
            }
            publishedView = view
            publishedDefaultBackendKey = seed.defaultBackendKey
            PluginCatalogState.Ready(seed.snapshot).also { mutableState.value = it }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (problem: Throwable) {
            publishedView = null
            publishedDefaultBackendKey = null
            PluginCatalogState.Failed(boundedReason(problem)).also { mutableState.value = it }
        }
    }

    /** Same-generation backend view; null until and unless [state] reaches READY. */
    fun readyBackendView(): BackendCatalogView? = publishedView

    /** Explicit index default; never inferred from descriptor ordering. */
    fun readyDefaultBackendKey(): String? = publishedDefaultBackendKey

    private fun boundedReason(problem: Throwable): String {
        val raw = problem.message?.takeIf(String::isNotBlank)
            ?: problem::class.java.simpleName.takeIf(String::isNotBlank)
            ?: "plugin catalog initialization failed"
        val printable = buildString {
            raw.forEach { character ->
                append(if (character == '\n' || character == '\r' || character.isISOControl()) ' ' else character)
            }
        }.trim().ifEmpty { "plugin catalog initialization failed" }
        val bytes = printable.encodeToByteArray()
        if (bytes.size <= MAX_REASON_BYTES) return printable
        var end = printable.length
        while (end > 0 && printable.substring(0, end).encodeToByteArray().size > MAX_REASON_BYTES) end--
        if (end > 0 && printable[end - 1].isHighSurrogate()) end--
        return printable.substring(0, end).ifEmpty { "plugin catalog initialization failed" }
    }

    private companion object {
        const val MAX_REASON_BYTES = 4096
    }
}

private fun Char.isHighSurrogate(): Boolean = code in 0xd800..0xdbff
