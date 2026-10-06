package com.ghostlock.app.ui

import androidx.annotation.StringRes
import com.ghostlock.app.R
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.plugin.PluginConfigValidator
import com.ghostlock.app.data.plugin.PluginDescriptor
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginParamType
import com.ghostlock.app.data.plugin.PluginValue

/** Severity of one line in a plugin's check report. */
enum class PluginIssueLevel { Error, Warn, Info }

/**
 * One line of the per-plugin check report. `Error` means the document build
 * would FAIL for this plugin while it is selected; `Warn` means the plugin is
 * usable but something will not happen as the user might expect; `Info` is
 * context (loaded / not loaded / not selected).
 *
 * The line is a resource plus its format args, never a pre-rendered sentence:
 * the page is localized while a log line is not. An arg may itself be a
 * [PluginLine], so a word the projection does not hold (the native default
 * method ladder) still comes from the device locale.
 */
data class PluginIssueRow(
    val level: PluginIssueLevel,
    @StringRes val resId: Int,
    val args: List<Any> = emptyList(),
) {
    init {
        /* 0 is not a resource: rendering it crashed a real device. */
        require(resId != 0) { "a report line needs a resource, not 0" }
    }
}

/**
 * One header value. DATA is what the registry or the probe reports — an id, a
 * digest, a path, a version, a token — and is NEVER translated. WORDS are the
 * page's own copy ("80 bytes", "yes", "loads"), so they are a resource. The
 * split is deliberate: a value that looks like a word but is wire data (the
 * `"true"/"false"` of [pluginValueText]) must stay data, or the editor can no
 * longer parse it back.
 */
sealed interface PluginHeaderValue {
    /** Registry/probe data, rendered exactly as it is. */
    data class Data(val text: String) : PluginHeaderValue

    /** A word the page writes: resource plus format args. */
    data class Words(
        @StringRes val resId: Int,
        val args: List<Any> = emptyList(),
    ) : PluginHeaderValue {
        init {
            /* 0 is not a resource: rendering it crashed a real device. */
            require(resId != 0) { "a header word needs a resource, not 0" }
        }
    }
}

/** One label/value line of the detail header. The LABEL is a resource too. */
data class PluginHeaderRow(@StringRes val labelRes: Int, val value: PluginHeaderValue) {
    init {
        /* 0 is not a resource: rendering it crashed a real device. */
        require(labelRes != 0) { "a header label needs a resource, not 0" }
    }
}

/** Everything the per-plugin page renders, all of it derived. */
data class PluginDetailState(
    val id: String,
    val header: List<PluginHeaderRow> = emptyList(),
    val params: List<PluginParamRow> = emptyList(),
    val extracts: List<PluginExtractRow> = emptyList(),
    val specs: List<PluginSpecRow> = emptyList(),
    val issues: List<PluginIssueRow> = emptyList(),
) {
    /** The problems that would fail the document build while it is selected. */
    val errors: List<PluginIssueRow> get() = issues.filter { it.level == PluginIssueLevel.Error }
}

/**
 * Read-only extractor value (P2): the extractor produces these, the App never
 * edits them (hand-typed offsets would bypass the extractor's authority).
 */
data class PluginExtractRow(
    val name: String,
    val type: PluginParamType,
    val required: Boolean,
    /** Null = the extractor has not produced this key yet. */
    val value: String?,
)

/**
 * One declared extraction (P2 `spec` row). READ-ONLY: the plugin declares it,
 * the extractor executes it, and the page only reports it. [methods] empty means
 * the native default ladder; [details] keeps the raw tokens (`-` columns stay
 * absent) because the DEFAULTS are native's authority, not the App's.
 */
data class PluginSpecRow(
    val name: String,
    val type: PluginParamType,
    val required: Boolean,
    /** Ordered try list as declared; empty = the default ladder. */
    val methods: List<String>,
    /** anchor/scope/pattern/hit/capture/width/signed/base/max_scan, raw tokens. */
    val details: String,
    /** Null = the extractor has not produced this value yet. */
    val value: String?,
)

/** The declared extraction specs, in declaration order. */
fun pluginSpecRows(
    descriptor: PluginDescriptor,
    extracts: Map<String, PluginValue>,
): List<PluginSpecRow> = descriptor.specs.map { spec ->
    val details = buildList {
        spec.anchor?.let { add("anchor=" + it) }
        spec.scope?.let { add("scope=" + it) }
        spec.pattern?.let { add("pattern=" + it) }
        spec.hit?.let { add("hit=" + it) }
        spec.capture?.let { add("capture=" + it) }
        spec.width?.let { add("width=" + it) }
        spec.signed?.let { add("signed=" + if (it) "1" else "0") }
        spec.base?.let { add("base=" + it) }
        spec.maxScan?.let { add("max_scan=" + it) }
    }.joinToString(" ")
    PluginSpecRow(
        name = spec.name,
        type = spec.type,
        required = spec.required,
        methods = spec.methods,
        details = details,
        value = extracts[spec.name]?.let { pluginValueText(it) },
    )
}

/** One declared extraction line, for the detail page. */
fun pluginSpecSummary(row: PluginSpecRow, texts: PluginTexts): String = buildString {
    append(row.name)
    append(" · ").append(row.type.text)
    if (row.required) append(" · ").append(texts.required)
    append(" · ").append(if (row.methods.isEmpty()) texts.defaultLadder else row.methods.joinToString(" → "))
    if (row.details.isNotEmpty()) append(" · ").append(row.details)
    append(" · ").append(row.value ?: texts.unresolved)
}

/** Detail-page header: identity, integrity and THIS run's intent. */
fun pluginHeaderRows(
    entry: PluginManifestEntry,
    descriptor: PluginDescriptor?,
    installedPath: String,
    selected: Boolean,
): List<PluginHeaderRow> = buildList {
    add(PluginHeaderRow(R.string.plugin_header_id, PluginHeaderValue.Data(entry.id)))
    add(PluginHeaderRow(R.string.plugin_header_version, PluginHeaderValue.Data(entry.version)))
    add(
        PluginHeaderRow(
            R.string.plugin_header_abi,
            PluginHeaderValue.Data(entry.abiVersion.toString()),
        ),
    )
    if (descriptor != null) {
        /* The unit is the page's word, the number is data. */
        add(
            PluginHeaderRow(
                R.string.plugin_header_size,
                PluginHeaderValue.Words(
                    R.string.plugin_header_size_bytes,
                    listOf(descriptor.size.toString()),
                ),
            ),
        )
    }
    /* The FULL digest, not the prefix: it is what the registry pins and what an
     * external check compares against. */
    add(PluginHeaderRow(R.string.plugin_header_sha256, PluginHeaderValue.Data(entry.sha256)))
    add(PluginHeaderRow(R.string.plugin_header_path, PluginHeaderValue.Data(installedPath)))
    add(PluginHeaderRow(R.string.plugin_header_stage, PluginHeaderValue.Data(entry.stage ?: "-")))
    add(
        PluginHeaderRow(
            R.string.plugin_header_enabled,
            PluginHeaderValue.Words(
                if (entry.enabled) R.string.plugin_header_yes else R.string.plugin_header_no,
            ),
        ),
    )
    add(
        PluginHeaderRow(
            R.string.plugin_header_this_run,
            PluginHeaderValue.Words(
                if (selected) R.string.plugin_header_loads else R.string.plugin_header_not_loaded,
            ),
        ),
    )
    if (descriptor != null) {
        add(
            PluginHeaderRow(
                R.string.plugin_header_host_abi,
                PluginHeaderValue.Data(descriptor.hostAbiVersion.toString()),
            ),
        )
        add(
            PluginHeaderRow(
                R.string.plugin_header_stages,
                PluginHeaderValue.Data(descriptor.stages.sorted().joinToString(",")),
            ),
        )
        add(
            PluginHeaderRow(
                R.string.plugin_header_required_caps,
                PluginHeaderValue.Data(descriptor.requiredCaps.sorted().joinToString(",")),
            ),
        )
        add(
            PluginHeaderRow(
                R.string.plugin_header_host_caps,
                PluginHeaderValue.Data(descriptor.hostCaps.sorted().joinToString(",")),
            ),
        )
    }
}

/** Read-only extractor values, in declaration order (see [PluginExtractRow]). */
fun pluginExtractRows(
    descriptor: PluginDescriptor,
    extracts: Map<String, PluginValue>,
): List<PluginExtractRow> = descriptor.extract.map { param ->
    PluginExtractRow(
        name = param.name,
        type = param.type,
        required = param.required,
        value = extracts[param.name]?.let { pluginValueText(it) },
    )
}

/**
 * The per-plugin check report.
 *
 * The stage availability comes from the PROBE (`stage_availability` × the
 * currently selected backend) — there is deliberately no Kotlin table saying
 * which backend runs which stage.
 */
fun pluginIssueRows(
    descriptor: PluginDescriptor?,
    entry: PluginManifestEntry,
    overrides: Map<String, PluginValue>,
    extracts: Map<String, PluginValue>,
    selectedBackend: BackendKind?,
    selected: Boolean,
    /** The probe's own reason the module could not be described, when it has one. */
    describeFailure: String? = null,
): List<PluginIssueRow> = buildList {
    if (descriptor == null) {
        add(
            if (describeFailure != null) {
                /* The real reason, not the generic "not described yet". */
                PluginIssueRow(
                    PluginIssueLevel.Error,
                    R.string.plugin_issue_describe_failed,
                    listOf(describeFailure),
                )
            } else {
                PluginIssueRow(PluginIssueLevel.Error, R.string.plugin_issue_no_descriptor)
            },
        )
        return@buildList
    }
    /* Fail-closed statements first: these stop the document build. */
    for (reject in descriptor.rejects) {
        add(
            PluginIssueRow(
                PluginIssueLevel.Error,
                R.string.plugin_issue_host_rejects,
                listOf(reject),
            ),
        )
    }
    if (descriptor.id != entry.id) {
        add(
            PluginIssueRow(
                PluginIssueLevel.Error,
                R.string.plugin_issue_id_mismatch,
                listOf(descriptor.id, entry.id),
            ),
        )
    }
    val configuredStage = entry.stage
    if (configuredStage != null && configuredStage !in descriptor.stages) {
        add(
            PluginIssueRow(
                PluginIssueLevel.Error,
                R.string.plugin_issue_stage_unregistered,
                listOf(configuredStage),
            ),
        )
    }
    for (error in PluginConfigValidator.validate(descriptor, entry.enabled, entry.stage, overrides)) {
        add(
            PluginIssueRow(
                PluginIssueLevel.Error,
                R.string.plugin_issue_field_error,
                listOf(error.path, error.reason),
            ),
        )
    }
    /* Warnings: usable, but not what the user may expect. */
    /* The matrix is the probe's; the App only looks the selected backend up. */
    val unavailable = stageNote(descriptor, entry, selectedBackend)
    if (unavailable != null) {
        add(PluginIssueRow(PluginIssueLevel.Warn, unavailable.resId, unavailable.args))
    }
    val missingCaps = descriptor.requiredCaps - descriptor.hostCaps
    if (missingCaps.isNotEmpty()) {
        add(
            PluginIssueRow(
                PluginIssueLevel.Warn,
                R.string.plugin_issue_missing_caps,
                listOf(missingCaps.sorted().joinToString(",")),
            ),
        )
    }
    for (param in descriptor.extract) {
        if (extracts[param.name] == null) {
            add(
                PluginIssueRow(
                    PluginIssueLevel.Warn,
                    R.string.plugin_issue_extract_unresolved,
                    listOf(param.name),
                ),
            )
        }
    }
    /* P2 declarations: an unresolved REQUIRED extraction fails the plugin;
     * an unresolved optional one is only a warning. */
    for (spec in descriptor.specs) {
        if (extracts[spec.name] != null) continue
        /* An empty method list is the native default ladder, and that is a WORD:
         * the arg stays a nested resource until the page renders it. */
        val methods = if (spec.methods.isEmpty()) {
            PluginLine(R.string.plugin_text_default_ladder)
        } else {
            spec.methods.joinToString(",")
        }
        add(
            PluginIssueRow(
                if (spec.required) PluginIssueLevel.Error else PluginIssueLevel.Warn,
                R.string.plugin_issue_spec_unresolved,
                listOf(spec.name, methods),
            ),
        )
    }
    /* Context. */
    if (!entry.enabled) {
        add(
            PluginIssueRow(PluginIssueLevel.Info, R.string.plugin_issue_disabled),
        )
    } else if (!selected) {
        add(
            PluginIssueRow(PluginIssueLevel.Info, R.string.plugin_issue_unselected),
        )
    } else {
        add(
            PluginIssueRow(PluginIssueLevel.Info, R.string.plugin_issue_will_load),
        )
    }
}

/** One read-only extract line: key, type, requirement and value (or the word for an unresolved one). */
fun pluginExtractSummary(row: PluginExtractRow, texts: PluginTexts): String = buildString {
    append(row.name)
    append(" · ").append(row.type.text)
    if (row.required) append(" · ").append(texts.required)
    append(" · ").append(row.value ?: texts.unresolved)
}

/**
 * Selected plugins whose problems would fail the document build, for the
 * summary next to the run button — the user must see this BEFORE running.
 */
fun selectedPluginErrors(rows: List<PluginRow>): List<String> =
    rows.filter { it.selected && !it.runUsable }.map { it.id }
