package com.ghostlock.app.data.plugin

/**
 * Resolves the run-level selection into what the DOCUMENT BUILD needs, running
 * the probe ON DEMAND for a selected plugin whose descriptor is not cached yet.
 *
 * The cache is an optimization, never a prerequisite: the first build after a
 * fresh start (or after the module changed on disk) describes what it needs. A
 * failure is a [PluginSelection.Blocked] carrying the probe's own reason — NEVER
 * an exception. The old `error(...)` here crashed the whole process on every
 * document build when an enabled plugin had no cached descriptor.
 *
 * Apart from the injected [describe] call this is pure, so the P0 behaviour is
 * unit-tested on the JVM: only SELECTED plugins are ever described, a cache hit
 * is not re-probed, and a failure names the plugin.
 */
internal object PluginSelectionResolver {

    /** The selection plus the descriptor cache as it stands after resolving. */
    data class Resolution(
        val selection: PluginSelection,
        val descriptors: Map<String, PluginDescriptor>,
    )

    suspend fun resolve(
        entries: List<PluginManifestEntry>,
        runSelection: Set<String>?,
        cached: Map<String, PluginDescriptor>,
        describe: suspend (PluginManifestEntry) -> PluginDescribeResult,
    ): Resolution {
        val selected = PluginRunSelection.of(entries, runSelection)
        if (selected.isEmpty()) return Resolution(PluginSelection.Ready(emptyList()), cached)
        val descriptors = cached.toMutableMap()
        val ready = mutableListOf<EnabledPlugin>()
        val reasons = mutableListOf<String>()
        for (entry in selected) {
            /* A cached descriptor must describe THIS module: a stale entry for
             * another id is re-described rather than trusted. */
            val known = descriptors[entry.id]?.takeIf { it.id == entry.id }
            if (known != null) {
                ready += EnabledPlugin(entry, known)
                continue
            }
            when (val described = describe(entry)) {
                is PluginDescribeResult.Described -> {
                    descriptors[entry.id] = described.descriptor
                    ready += EnabledPlugin(entry, described.descriptor)
                }

                is PluginDescribeResult.Failed -> reasons += entry.id + ": " + described.reason
            }
        }
        val selection = if (reasons.isEmpty()) {
            PluginSelection.Ready(ready)
        } else {
            PluginSelection.Blocked(reasons)
        }
        return Resolution(selection, descriptors)
    }
}
