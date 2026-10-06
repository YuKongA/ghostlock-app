package com.ghostlock.app.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.data.component.BackendKind
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

/**
 * The words the pure projections spell out ([pluginParamSummary],
 * [pluginSpecSummary], [pluginExtractSummary]). The page resolves them from
 * `plugin_text_*`, so no projection carries a hard-coded English word.
 */
data class PluginTexts(
    val required: String,
    val unresolved: String,
    val defaultLadder: String,
)

/**
 * One line the page still has to resolve: the resource and its format args,
 * never a pre-rendered sentence — the page is localized while a log line is not.
 *
 * An arg may itself be a [PluginLine]. That is what keeps the projections free
 * of a Context: the plugin report is built in the ViewModel (no Context, no
 * `getString`) and the page resolves the nested resource when it renders, so a
 * word the projection does not hold (the native default method ladder) still
 * comes from the device locale. Do not replace such an arg with a string: that
 * is exactly how English leaks back into a localized page.
 */
data class PluginLine(
    @StringRes val resId: Int,
    val args: List<Any> = emptyList(),
) {
    init {
        /* 0 is not a resource: rendering it crashed a real device. */
        require(resId != 0) { "a report line needs a resource, not 0" }
    }
}

/**
 * Resolves one line for the page. A nested [PluginLine] arg is resolved first,
 * so a word the projection does not hold still comes from the device locale.
 */
@Composable
fun pluginLineText(line: PluginLine): String {
    val args = line.args.map { arg -> if (arg is PluginLine) pluginLineText(arg) else arg }
    return stringResource(line.resId, *args.toTypedArray())
}

/**
 * The row summary: the page joins the resource parts with " · ". `map` is
 * inline (a composable call is allowed inside it), `joinToString` is not.
 */
@Composable
fun pluginSummaryText(parts: List<PluginLine>): String =
    parts.map { part -> pluginLineText(part) }.joinToString(" · ")

/** Resolves one header value: data as it stands, a page word through resources. */
@Composable
fun pluginHeaderValueText(value: PluginHeaderValue): String = when (value) {
    is PluginHeaderValue.Data -> value.text
    is PluginHeaderValue.Words -> stringResource(value.resId, *value.args.toTypedArray())
}

/** One registry row as the settings page renders it. */
data class PluginRow(
    val id: String,
    val version: String,
    /** Short prefix of the pinned hash; the full value stays in the registry. */
    val sha256Short: String,
    val enabled: Boolean,
    /**
     * True when THIS run loads the plugin. Only enabled rows can be selected;
     * `null` selection means the default (every enabled plugin).
     */
    val selected: Boolean = false,
    /**
     * True when THIS RUN could load the plugin: a probe descriptor exists and
     * the contract validator found no error. It is what the run gate counts
     * ([selectedPluginErrors]) — NOT whether the user may edit the row.
     */
    val runUsable: Boolean,
    /**
     * True when the user may change the ENABLE flag. A registry row is always
     * toggleable: a description problem is REPORTED ([blockedReason]), never a
     * lock-out. Gating the enable switch on [runUsable] trapped the user — a
     * disabled plugin has no fresh description, so the switch greyed out and the
     * plugin could not be turned back on without leaving the page.
     */
    val toggleable: Boolean,
    /** Why the row is greyed, as a resource: never a pre-rendered sentence. */
    val blockedReason: PluginLine?,
    /**
     * Set when the plugin's stage is not available on the SELECTED backend,
     * per the matrix the native probe reports. Never set from a Kotlin-side
     * table: no probe data (or an unmapped backend) means no note. The check
     * report renders the same line.
     */
    val stageNote: PluginLine? = null,
    /**
     * The summary PARTS the page joins with " · " (`abi 1 · stages …`): the
     * data with the words around it. Parts, not a sentence, because the words
     * are resources and the ViewModel that builds the row has no Context.
     */
    val summary: List<PluginLine>,
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
fun pluginParamSummary(row: PluginParamRow, texts: PluginTexts): String = buildString {
    append(row.name).append(" (").append(row.type.text)
    if (row.required) append(", ").append(texts.required)
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

/**
 * The summary parts of a described row: data (version, tokens) with the words
 * the page puts around it. The page joins them, so no part is a sentence.
 */
private fun PluginDescriptor.summaryParts(entry: PluginManifestEntry): List<PluginLine> = buildList {
    add(PluginLine(R.string.plugin_summary_abi, listOf(entry.abiVersion.toString())))
    if (stages.isNotEmpty()) {
        add(PluginLine(R.string.plugin_summary_stages, listOf(stages.sorted().joinToString(","))))
    }
    if (requiredCaps.isNotEmpty()) {
        add(
            PluginLine(
                R.string.plugin_summary_caps,
                listOf(requiredCaps.sorted().joinToString(",")),
            ),
        )
    }
}

/**
 * Registry rows in id order. A row without a probe descriptor cannot be enabled
 * yet (the schema is unknown), and a row whose descriptor the host would reject
 * is greyed with the first reason.
 */
/**
 * The probe reports a backend by its SHORT token (native `backend_short_token`,
 * e.g. "43499") while the App carries the full identity token; the derivation is
 * a documented prefix strip, not a second table. A backend with no probe entry
 * simply has no availability information.
 */
internal fun probeBackendToken(kind: BackendKind): String = kind.token.removePrefix("cve_2026_")

/**
 * Why the plugin's configured stage cannot run on the selected backend, when the
 * probe reports a matrix. Absent matrix, unknown backend or absent stage mean
 * "no information", never a fabricated block.
 *
 * Returned as a resource with its args, so the check report and the settings row
 * share ONE wording: the report renders the line itself, the row wraps it in
 * [R.string.plugin_stage_unavailable].
 */
internal fun stageNote(
    descriptor: PluginDescriptor?,
    entry: PluginManifestEntry,
    selectedBackend: BackendKind?,
): PluginLine? {
    if (descriptor == null || selectedBackend == null) return null
    val availability = descriptor.stageAvailability
    if (availability.isEmpty()) return null
    val stages = availability[probeBackendToken(selectedBackend)] ?: return null
    val stage = entry.stage ?: return null
    if (stage in stages) return null
    return PluginLine(
        resId = R.string.plugin_issue_stage_unavailable,
        args = listOf(stage, selectedBackend.token, stages.joinToString(",")),
    )
}

fun pluginRows(
    entries: List<PluginManifestEntry>,
    descriptors: Map<String, PluginDescriptor>,
    selectedBackend: BackendKind? = null,
    runSelection: Set<String>? = null,
    /** Plugin id → the probe's own reason it could not be described. */
    describeFailures: Map<String, String> = emptyMap(),
): List<PluginRow> = entries.sortedBy { it.id }.map { entry ->
    val descriptor = descriptors[entry.id]
    val describeFailure = describeFailures[entry.id]
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
        selected = entry.enabled && (runSelection == null || entry.id in runSelection),
        runUsable = descriptor != null && errors.isEmpty(),
        /* An installed row can always be enabled/disabled; see [PluginRow.toggleable]. */
        toggleable = true,
        blockedReason = when {
            /* The probe's own reason beats the generic "not described yet": the
             * user must see WHY (stderr/reject line) to be able to fix it. */
            descriptor == null && describeFailure != null -> PluginLine(
                R.string.plugin_issue_describe_failed,
                listOf(describeFailure),
            )

            descriptor == null -> PluginLine(R.string.plugin_issue_no_descriptor)
            errors.isNotEmpty() -> errors.first().let {
                PluginLine(R.string.plugin_issue_field_error, listOf(it.path, it.reason))
            }
            else -> null
        },
        stageNote = stageNote(descriptor, entry, selectedBackend),
        summary = if (descriptor == null) {
            listOf(PluginLine(R.string.plugin_summary_abi, listOf(entry.abiVersion.toString())))
        } else {
            descriptor.summaryParts(entry)
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
