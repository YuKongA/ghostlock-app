package com.ghostlock.app.data.plugin

/** One rejected plugin configuration value; [path] is the document path. */
data class PluginConfigError(val path: String, val reason: String)

/**
 * P1 plugin configuration validation (interface freeze 2026-10-05).
 *
 * The plugin's own parameter table is the default authority; HOCON and the UI
 * only write explicit overrides. This validator is the single Kotlin gate that
 * decides whether a plugin configuration may be written into the document:
 *
 *  - unknown parameter names are rejected (no silent pass-through);
 *  - a value must match the declared type;
 *  - a required parameter without a default must be supplied;
 *  - strings are bounded by the GLKv3 limit (256 UTF-8 bytes, same as R2);
 *  - an enabled plugin must be acceptable to this host: every declared stage and
 *    required capability is implemented, and the probe reported no rejection.
 *
 * Pure and side-effect free, so the UI, the exporter and the tests share it.
 */
object PluginConfigValidator {
    /** Native GLKv3 string bound (glkv3::kMaxStringBytes). */
    const val MAX_STRING_BYTES = 256

    fun validate(
        descriptor: PluginDescriptor,
        enabled: Boolean,
        stage: String?,
        overrides: Map<String, PluginValue>,
    ): List<PluginConfigError> {
        val errors = mutableListOf<PluginConfigError>()
        val prefix = "plugin." + descriptor.id

        if (stage != null && stage !in descriptor.stages) {
            errors += PluginConfigError(
                prefix + ".stage",
                "the plugin does not register this stage: " + stage,
            )
        }
        if (!descriptor.usable) {
            errors += PluginConfigError(
                prefix,
                "the host would reject this module at registration: " +
                    descriptor.rejects.joinToString("; "),
            )
        }
        if (enabled) {
            val missingCaps = (descriptor.requiredCaps - descriptor.hostCaps).sorted()
            if (missingCaps.isNotEmpty()) {
                errors += PluginConfigError(
                    prefix + ".required_caps",
                    "the host does not implement: " + missingCaps.joinToString(","),
                )
            }
            val missingStages = (descriptor.stages - descriptor.hostStages).sorted()
            if (missingStages.isNotEmpty()) {
                errors += PluginConfigError(
                    prefix + ".stages",
                    "the host does not implement: " + missingStages.joinToString(","),
                )
            }
        }

        val declared = descriptor.paramsByName
        for ((name, value) in overrides) {
            val path = prefix + ".params." + name
            val param = declared[name]
            if (param == null) {
                errors += PluginConfigError(path, "the plugin declares no such parameter")
                continue
            }
            if (value.type != param.type) {
                errors += PluginConfigError(
                    path,
                    "expected " + param.type.text + ", got " + value.type.text,
                )
                continue
            }
            if (value is PluginValue.Str &&
                value.value.toByteArray(Charsets.UTF_8).size > MAX_STRING_BYTES
            ) {
                errors += PluginConfigError(
                    path,
                    "string exceeds " + MAX_STRING_BYTES + " bytes",
                )
            }
        }
        for (param in descriptor.params) {
            if (param.required && param.defaultValue == null && param.name !in overrides) {
                errors += PluginConfigError(
                    prefix + ".params." + param.name,
                    "required parameter has no default and no value",
                )
            }
        }
        return errors
    }
}
