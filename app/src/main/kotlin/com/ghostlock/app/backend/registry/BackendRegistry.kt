package com.ghostlock.app.backend.registry

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe
import com.ghostlock.app.backend.backends.lua.BackendCustom
import com.ghostlock.app.backend.declarations.BackendPlugin
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.run.RunCapability

/**
 * Immutable same-APK backend membership and key lookup authority.
 *
 * The registry contains the disabled restricted Lua frontend and, in debug builds only, the diagnostic
 * DebugProbe. No real-attack backend is registered; reconnecting one requires a new approved L-level plan.
 */
object BackendRegistry {
    private val plugins: List<BackendPlugin> = buildList {
        add(BackendCustom.plugin)
        if (BuildConfig.DEBUG) add(BackendDebugProbe.plugin)
    }.also { entries ->
        require(entries.map { plugin -> plugin.spec.key }.toSet().size == entries.size) {
            "duplicate immutable backend key"
        }
    }

    /** Neutral immutable specs in stable presentation order. */
    val specs: List<BackendSpec> = plugins.map(BackendPlugin::spec)

    /** Resolves a neutral spec by exact stable key. */
    fun spec(key: String?): BackendSpec? = specs.firstOrNull { candidate -> candidate.key == key }

    /** Resolves current resource-free readiness by exact stable key. */
    fun availability(key: String): BackendAvailability? = plugin(key)?.availability()

    /** Number of selectable product-root entries; R3 deliberately permits zero. */
    val productionBackendCount: Int = specs.count { spec ->
        spec.runCapability == RunCapability.PRODUCT_ROOT && spec.selectable
    }

    /** Resolves an internal plugin for RunHost prepare; UI and wire never receive this object. */
    internal fun plugin(key: String): BackendPlugin? = plugins.firstOrNull { candidate ->
        candidate.spec.key == key
    }
}
