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
 *  - the stage is the registry's pin when it has one, else the plugin's ONLY
 *    declared stage: native fails the run with StageMissing when the document
 *    carries no stage, so "no stage" is never a valid emission;
 *  - `module_path` is the registry row, relative to
 *    `<GHOSTLOCK_HOME>/countermeasures` (no extra layer);
 *  - `module_hash` is the registry's pinned lower-case hex digest;
 *  - a parameter not declared by the descriptor is refused, a value whose type
 *    differs from the declaration is refused, and only explicitly overridden
 *    values are emitted (defaults stay the descriptor's authority, as in R1);
 *  - extractor values (P2) follow exactly the same rule against the descriptor's
 *    extract rows: a key the module does not declare is refused, an unresolved
 *    key is simply absent from the document.
 */
data class PluginEmission(
    val id: String,
    val stage: String?,
    val modulePath: String,
    val moduleHash: String,
    /** Declared parameters with an explicit override, in declaration order. */
    val params: List<Param>,
    /**
     * Declared extractor keys with a value, in declaration order (P2). Empty
     * means the extractor produced nothing for this plugin, and then no
     * `<id>.extract.*` key is emitted at all.
     */
    val extract: List<Param> = emptyList(),
) {
    data class Param(val name: String, val value: PluginValue)

    companion object {
        private val SHA256_HEX = Regex("[0-9a-f]{64}")

        /** Values of the declared keys, in declaration order; type-checked. */
        private fun declared(
            id: String,
            schema: List<PluginParam>,
            values: Map<String, PluginValue>,
            label: String,
        ): List<Param> = schema.mapNotNull { param ->
            val value = values[param.name] ?: return@mapNotNull null
            require(value.type == param.type) {
                "plugin " + id + " " + label + " " + param.name +
                    " expects " + param.type.text + ", got " + value.type.text
            }
            Param(param.name, value)
        }

        /** Null when the plugin is disabled; throws on any contract violation. */
        fun of(
            entry: PluginManifestEntry,
            descriptor: PluginDescriptor,
            overrides: Map<String, PluginValue> = emptyMap(),
            extracts: Map<String, PluginValue> = emptyMap(),
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
            /* A registry pin wins; otherwise the plugin's own default policy is
             * its SINGLE declared stage. Several declared stages cannot be guessed
             * (that is a user choice and the App has no picker yet), so the
             * disagreement is refused HERE, with a reason the page can show,
             * instead of being emitted stage-less for native to reject. */
            val stage = entry.stage ?: descriptor.stages.singleOrNull()
            require(stage != null) {
                "plugin " + entry.id + " declares " + descriptor.stages.size +
                    " stages (" + descriptor.stages.sorted().joinToString(",") +
                    "); none is pinned in the registry"
            }
            require(stage in descriptor.stages) {
                "plugin " + entry.id + " does not register stage " + stage
            }
            val unknown = overrides.keys - descriptor.paramsByName.keys
            require(unknown.isEmpty()) {
                "plugin " + entry.id + " declares no such parameter: " + unknown.sorted().joinToString(",")
            }
            val unknownExtract = extracts.keys - descriptor.extractByName.keys
            require(unknownExtract.isEmpty()) {
                "plugin " + entry.id + " declares no such extract key: " +
                    unknownExtract.sorted().joinToString(",")
            }
            val params = declared(entry.id, descriptor.params, overrides, "parameter")
            val extract = declared(entry.id, descriptor.extract, extracts, "extract key")
            return PluginEmission(
                id = entry.id,
                stage = stage,
                modulePath = entry.modulePath,
                moduleHash = entry.sha256,
                params = params,
                extract = extract,
            )
        }
    }
}
