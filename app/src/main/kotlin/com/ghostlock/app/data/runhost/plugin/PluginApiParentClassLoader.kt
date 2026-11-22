package com.ghostlock.app.data.runhost.plugin

import com.ghostlock.plugin.api.v1.DiagnosticBackendEntryPoint

/**
 * Restricted parent for one package-private plugin classloader.
 *
 * Platform classes remain boot-owned, the shared API is resolved from the Host API loader, and
 * every other Host implementation namespace is deliberately invisible.
 */
internal class PluginApiParentClassLoader(
    private val apiLoader: ClassLoader = requireNotNull(DiagnosticBackendEntryPoint::class.java.classLoader),
) : ClassLoader(apiLoader.parent) {
    override fun loadClass(name: String, resolve: Boolean): Class<*> =
        synchronized(this) {
            findLoadedClass(name)?.let { return@synchronized it }
            when {
                PLATFORM_PREFIXES.any(name::startsWith) -> super.loadClass(name, resolve)
                name.startsWith(API_PREFIX) -> apiLoader.loadClass(name)
                else -> throw ClassNotFoundException("plugin parent rejects $name")
            }
        }

    private companion object {
        const val API_PREFIX = "com.ghostlock.plugin.api.v1."
        val PLATFORM_PREFIXES = listOf("java.", "javax.", "android.", "dalvik.", "libcore.")
    }
}
