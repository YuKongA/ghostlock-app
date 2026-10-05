package com.ghostlock.app.ui

import com.ghostlock.app.data.plugin.PluginConfigError
import com.ghostlock.app.data.plugin.PluginConfigValidator
import com.ghostlock.app.data.plugin.PluginDescriptor
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginParamType
import com.ghostlock.app.data.plugin.PluginValue

/**
 * P1 plugin settings presentation (interface freeze 2026-10-05).
 *
 * Pure projections of the registry and the probe descriptor, so the page cannot
 * offer a plugin, a parameter or an enable action the contract does not allow;
 * PluginPresentationTest asserts them without an Android runtime.
 */

/** One registry row as the settings page renders it. */
data class PluginRow(
    val id: String,
    val version: String,
    /** Short prefix of the pinned hash; the full value stays in the registry. */
    val sha256Short: String,
    val enabled: Boolean,
    /** False = the row is shown greyed; [blockedReason] says why. */
    val selectable: Boolean,
    val blockedReason: String?,
    val summary: String,
)

/** How the page edits one parameter: a text field or a switch. */
enum class PluginParamEditor { Text, Switch }

/** One schema-driven advanced-setting row. */
data class PluginParamRow(
    /** Document path the editor writes: plugin.<id>.params.<name>. */
    val path: String,
    val name: String,
    val type: PluginParamType,
    /** Bool parameters are switches; every other kind is edited as text. */
    val editor: PluginParamEditor,
    val required: Boolean,
    val defaultText: String?,
    /** Current text: the override, else the declared default, else empty. */
    val value: String,
    val error: String?,
)

/**
 * One display line for a schema parameter: name, declared type, required flag,
 * then the declared default (or the current value) and any validation reason.
 * Pure, so the page cannot render a parameter the schema does not declare.
 */
fun pluginParamSummary(row: PluginParamRow): String = buildString {
    append(row.name).append(" (").append(row.type.text)
    if (row.required) append(", required")
    append(")")
    val shown = row.defaultText ?: row.value.takeIf { it.isNotEmpty() }
    if (shown != null) append(" — ").append(shown)
    row.error?.let { append(" — ").append(it) }
}

/** The text form the editor shows and parses back for a typed value. */
fun pluginValueText(value: PluginValue): String = when (value) {
    is PluginValue.UInt -> value.value.toString()
    is PluginValue.Int -> value.value.toString()
    is PluginValue.Bool -> if (value.value) "true" else "false"
    is PluginValue.Str -> value.value
}

private fun PluginDescriptor.summaryText(entry: PluginManifestEntry): String = buildString {
    append("abi ").append(entry.abiVersion)
    if (stages.isNotEmpty()) append(" · stages ").append(stages.sorted().joinToString(","))
    if (requiredCaps.isNotEmpty()) {
        append(" · caps ").append(requiredCaps.sorted().joinToString(","))
    }
}

/**
 * Registry rows in id order. A row without a probe descriptor cannot be enabled
 * yet (the schema is unknown), and a row whose descriptor the host would reject
 * is greyed with the first reason.
 */
fun pluginRows(
    entries: List<PluginManifestEntry>,
    descriptors: Map<String, PluginDescriptor>,
): List<PluginRow> = entries.sortedBy { it.id }.map { entry ->
    val descriptor = descriptors[entry.id]
    val errors = if (descriptor == null) {
        emptyList()
    } else {
        PluginConfigValidator.validate(
            descriptor = descriptor,
            enabled = true,
            stage = entry.stage,
            overrides = emptyMap(),
        )
    }
    PluginRow(
        id = entry.id,
        version = entry.version,
        sha256Short = entry.sha256.take(12),
        enabled = entry.enabled,
        selectable = descriptor != null && errors.isEmpty(),
        blockedReason = when {
            descriptor == null -> "the native probe has not described this module yet"
            errors.isNotEmpty() -> errors.first().let { it.path + ": " + it.reason }
            else -> null
        },
        summary = if (descriptor == null) {
            "abi " + entry.abiVersion
        } else {
            descriptor.summaryText(entry)
        },
    )
}

/**
 * Advanced-setting rows for one plugin, in declared order. A parameter the
 * plugin does not declare can never appear here, and a value that fails
 * validation is annotated with its reason.
 */
fun pluginParamRows(
    descriptor: PluginDescriptor,
    overrides: Map<String, PluginValue>,
    errors: List<PluginConfigError>,
): List<PluginParamRow> = descriptor.params.map { param ->
    val path = "plugin." + descriptor.id + ".params." + param.name
    val effective = overrides[param.name] ?: param.defaultValue
    PluginParamRow(
        path = path,
        name = param.name,
        type = param.type,
        editor = if (param.type == PluginParamType.Bool) {
            PluginParamEditor.Switch
        } else {
            PluginParamEditor.Text
        },
        required = param.required,
        defaultText = param.defaultValue?.let { pluginValueText(it) },
        value = effective?.let { pluginValueText(it) } ?: "",
        error = errors.firstOrNull { it.path == path }?.reason,
    )
}
