package com.ghostlock.app.data.plugin

/**
 * One ENABLED plugin as it is emitted into the GLKv3 document (P1 emission
 * batch, contract-design §3.14.7.5).
 *
 * The registry owns identity, the pinned hash and the enable flag; the probe
 * descriptor owns the parameter schema; the overrides map owns the values the
 * user actually set. The emission is the single place those three meet, and it
 * fails closed on any disagreement:
 *
 *  - a disabled plugin emits NOTHING (native rejects a disabled-but-present
 *    plugin, so emitting it would break the chain);
 *  - `module_path` is the registry row, relative to
 *    `<GHOSTLOCK_HOME>/countermeasures` (no extra layer);
 *  - `module_hash` is the registry's pinned lower-case hex digest;
 *  - a parameter not declared by the descriptor is refused, a value whose type
 *    differs from the declaration is refused, and only explicitly overridden
 *    values are emitted (defaults stay the descriptor's authority, as in R1).
 */
data class PluginEmission(
    val id: String,
    val stage: String?,
    val modulePath: String,
    val moduleHash: String,
    /** Declared parameters with an explicit override, in declaration order. */
    val params: List<Param>,
) {
    data class Param(val name: String, val value: PluginValue)

    companion object {
        private val SHA256_HEX = Regex("[0-9a-f]{64}")

        /** Null when the plugin is disabled; throws on any contract violation. */
        fun of(
            entry: PluginManifestEntry,
            descriptor: PluginDescriptor,
            overrides: Map<String, PluginValue> = emptyMap(),
        ): PluginEmission? {
            if (!entry.enabled) return null
            require(descriptor.id == entry.id) {
                "plugin descriptor " + descriptor.id + " does not describe " + entry.id
            }
            require(PluginPaths.isSafeModulePath(entry.modulePath)) {
                "plugin module path is not usable: " + entry.modulePath
            }
            require(SHA256_HEX.matches(entry.sha256)) {
                "plugin module hash is not lower-case hex: " + entry.sha256
            }
            require(descriptor.usable) {
                "plugin " + entry.id + " would be rejected: " + descriptor.rejects.joinToString("; ")
            }
            val stage = entry.stage
            if (stage != null) {
                require(stage in descriptor.stages) {
                    "plugin " + entry.id + " does not register stage " + stage
                }
            }
            val unknown = overrides.keys - descriptor.paramsByName.keys
            require(unknown.isEmpty()) {
                "plugin " + entry.id + " declares no such parameter: " + unknown.sorted().joinToString(",")
            }
            val params = descriptor.params.mapNotNull { param ->
                val value = overrides[param.name] ?: return@mapNotNull null
                require(value.type == param.type) {
                    "plugin " + entry.id + " parameter " + param.name +
                        " expects " + param.type.text + ", got " + value.type.text
                }
                Param(param.name, value)
            }
            return PluginEmission(
                id = entry.id,
                stage = stage,
                modulePath = entry.modulePath,
                moduleHash = entry.sha256,
                params = params,
            )
        }
    }
}
