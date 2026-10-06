package com.ghostlock.app.ui

import androidx.annotation.StringRes
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.R
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource

/** The three mutually exclusive custom-execution tiers (payload.tier). */
enum class PayloadTier(val token: String) {
    Exec("exec"),
    Script("script"),
    Ko("ko"),
    ;

    companion object {
        /** EXACT token lookup (the wire vocabulary), like every other vocabulary. */
        fun resolve(token: String?): PayloadTier? =
            token?.let { value -> entries.firstOrNull { it.token == value } }

        fun normalize(token: String?): String? = token?.trim()?.lowercase()
    }
}

/** One ko module of the payload (order is the load order). */
data class PayloadKoEntry(
    val name: String,
    /** Path relative to the App's no-backup payload directory. */
    val path: String,
    val sha256: String?,
)

/**
 * The payload as the App holds it (batch a: in-memory; persistence lands with the
 * wire owner, so nothing half-wired is written into the profile yet).
 *
 * `tier == null` is the DEFAULT choice, not a missing value: it means "no custom
 * content at all", so no `payload` section is emitted and NOTHING has to be
 * authorised. It is the initial selection. The other three tiers keep their
 * values while inactive, exactly like the plugin run selection keeps "installed"
 * and "loaded this run" apart.
 */
data class PayloadDraft(
    val tier: PayloadTier? = null,
    /** argv semantics: split on whitespace, NO shell. */
    val execCommand: String = "",
    val execSha256: String? = null,
    val scriptName: String? = null,
    val scriptPath: String? = null,
    val scriptSha256: String? = null,
    val koEntries: List<PayloadKoEntry> = emptyList(),
) {
    val active: Boolean
        get() = when (tier) {
            PayloadTier.Exec -> execCommand.isNotBlank()
            PayloadTier.Script -> scriptPath != null
            PayloadTier.Ko -> koEntries.isNotEmpty()
            null -> false
        }

    /** True only for a custom tier: the default needs no authorisation. */
    val needsAuthorisation: Boolean get() = tier != null
}

/** Maximum number of ko modules native accepts. */
const val PAYLOAD_MAX_KO = 8

/** `sha256:<64 hex>` value of the profile, or null when unset. */
private fun hashValue(sha256: String?): PluginValue? =
    sha256?.takeIf { it.isNotBlank() }?.let { PluginValue.Str(it) }

/**
 * The payload's profile values (`payload.*`). ONLY the selected tier appears,
 * so switching tiers is the switch: nothing has to be deleted to disable one.
 * Values are spelled exactly as the frozen contract names them; a blank hash is
 * omitted rather than written empty.
 */
fun payloadValues(draft: PayloadDraft): Map<String, PluginValue> {
    val values = linkedMapOf<String, PluginValue>()
    val tier = draft.tier ?: return values
    values["tier"] = PluginValue.Str(tier.token)
    when (tier) {
        PayloadTier.Exec -> {
            values["exec.command"] = PluginValue.Str(draft.execCommand)
            hashValue(draft.execSha256)?.let { values["exec.sha256"] = it }
        }

        PayloadTier.Script -> {
            draft.scriptPath?.let { values["script.path"] = PluginValue.Str(it) }
            hashValue(draft.scriptSha256)?.let { values["script.sha256"] = it }
        }

        PayloadTier.Ko -> {
            values["ko.count"] = PluginValue.UInt(draft.koEntries.size.toULong())
            draft.koEntries.forEachIndexed { index, entry ->
                values["ko." + index + ".path"] = PluginValue.Str(entry.path)
                hashValue(entry.sha256)?.let { values["ko." + index + ".sha256"] = it }
            }
        }
    }
    return values
}

/** `sha256` must be 64 lower-case hex when present. */
private val SHA256_HEX = Regex("[0-9a-f]{64}")

/**
 * One line of the page's own check list. The TEXT is a resource (the page is
 * localized) while the run LOG stays English: the two must never share a
 * string, or a log sentence ends up rendered in the UI (the bug the user saw).
 */
data class PayloadMessage(
    val level: PluginIssueLevel,
    @StringRes val resId: Int,
    val args: List<Any> = emptyList(),
) {
    init {
        /* 0 is not a resource: rendering it crashed a real device. */
        require(resId != 0) { "a check line needs a resource, not 0" }
    }
}

/** The page's check list, fully localized. */
fun payloadMessages(draft: PayloadDraft): List<PayloadMessage> = buildList {
    val tier = draft.tier
    if (tier == null) {
        add(PayloadMessage(PluginIssueLevel.Info, R.string.payload_check_default))
        return@buildList
    }
    when (tier) {
        PayloadTier.Exec -> {
            if (draft.execCommand.isBlank()) {
                add(PayloadMessage(PluginIssueLevel.Error, R.string.payload_check_command_empty))
            } else if (payloadArgv(draft.execCommand).isEmpty()) {
                add(PayloadMessage(PluginIssueLevel.Error, R.string.payload_check_command_empty))
            }
            if (draft.execCommand.contains('"') || draft.execCommand.contains('\'')) {
                add(PayloadMessage(PluginIssueLevel.Warn, R.string.payload_check_command_quotes))
            }
            add(hashMessage(draft.execSha256))
        }

        PayloadTier.Script -> {
            if (draft.scriptPath == null) {
                add(PayloadMessage(PluginIssueLevel.Error, R.string.payload_check_script_missing))
            }
            add(hashMessage(draft.scriptSha256))
        }

        PayloadTier.Ko -> {
            if (draft.koEntries.isEmpty()) {
                add(PayloadMessage(PluginIssueLevel.Error, R.string.payload_check_ko_missing))
            }
            if (draft.koEntries.size > PAYLOAD_MAX_KO) {
                add(
                    PayloadMessage(
                        PluginIssueLevel.Error,
                        R.string.payload_check_ko_too_many,
                        listOf(PAYLOAD_MAX_KO),
                    ),
                )
            }
            if (draft.koEntries.any { it.sha256 == null }) {
                add(PayloadMessage(PluginIssueLevel.Warn, R.string.payload_check_ko_unverified))
            }
        }
    }
}

private fun hashMessage(sha256: String?): PayloadMessage {
    if (sha256.isNullOrBlank()) {
        return PayloadMessage(PluginIssueLevel.Warn, R.string.payload_check_hash_unset)
    }
    if (!SHA256_HEX.matches(sha256)) {
        return PayloadMessage(PluginIssueLevel.Error, R.string.payload_check_hash_invalid)
    }
    return PayloadMessage(PluginIssueLevel.Info, R.string.payload_check_hash_ok, listOf(sha256.take(12)))
}

/**
 * The localized line shown next to the run button. Separate from
 * [payloadRunLogLine] on purpose: the log is English evidence, the UI is
 * localized text, and sharing one string leaked English into the page.
 */
@Composable
fun payloadRunSummaryText(draft: PayloadDraft): String = when (draft.tier) {
    null -> stringResource(R.string.payload_run_default)
    PayloadTier.Exec -> stringResource(
        R.string.payload_run_exec,
        payloadArgv(draft.execCommand).joinToString(" "),
    )

    PayloadTier.Script -> stringResource(
        R.string.payload_run_script,
        draft.scriptName ?: draft.scriptPath.orEmpty(),
    )

    PayloadTier.Ko -> stringResource(R.string.payload_run_ko, draft.koEntries.size)
}

/**
 * argv semantics, written down once: split on runs of whitespace, no quoting, no
 * escaping, no variable expansion, no shell. A path containing a space therefore
 * cannot be expressed, which the page says out loud.
 */
fun payloadArgv(command: String): List<String> =
    command.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }

/**
 * ko load order is the list order, and the user may change it: native loads the
 * modules in exactly this sequence.
 */
fun payloadKoMove(entries: List<PayloadKoEntry>, index: Int, delta: Int): List<PayloadKoEntry> {
    val target = index + delta
    if (index !in entries.indices || target !in entries.indices) return entries
    val mutable = entries.toMutableList()
    val moved = mutable.removeAt(index)
    mutable.add(target, moved)
    return mutable
}

fun payloadKoRemove(entries: List<PayloadKoEntry>, index: Int): List<PayloadKoEntry> =
    if (index in entries.indices) entries.filterIndexed { at, _ -> at != index } else entries

/**
 * The one-line summary shown next to the run button, and written to the log.
 * The DEFAULT tier has a line too: "nothing custom" is information the user must
 * see, not an absent line.
 */
/**
 * One row of the tier radio list (the interaction the user asked for: choose
 * one, its options open UNDERNEATH it).
 *
 * Exactly ONE row is selected, and ONLY the selected row carries an expansion,
 * so a row can never show fields that belong to another tier. The keys are
 * stable identifiers; the UI maps them to labels and to the draft's values, so
 * this projection stays resource-free and directly testable.
 */
data class PayloadTierRow(
    /** null = the default choice (no custom execution). */
    val tier: PayloadTier?,
    val selected: Boolean,
    /** Same as [selected]: the expansion belongs to the chosen row. */
    val expanded: Boolean,
    /** Inline text fields of this tier (empty when collapsed or default). */
    val fieldKeys: List<String> = emptyList(),
    /** Inline actions (the file pickers) of this tier. */
    val actionKeys: List<String> = emptyList(),
)

/**
 * The four radio rows in display order: the DEFAULT first (initial selection),
 * then exec, script and ko. The default has NO expansion at all — there is
 * nothing to configure — so its keys stay empty.
 */
fun payloadTierRows(draft: PayloadDraft): List<PayloadTierRow> {
    fun row(
        tier: PayloadTier?,
        fieldKeys: List<String> = emptyList(),
        actionKeys: List<String> = emptyList(),
    ): PayloadTierRow {
        val selected = draft.tier == tier
        return PayloadTierRow(
            tier = tier,
            selected = selected,
            expanded = selected,
            fieldKeys = if (selected) fieldKeys else emptyList(),
            actionKeys = if (selected) actionKeys else emptyList(),
        )
    }
    return listOf(
        row(null),
        row(PayloadTier.Exec, fieldKeys = listOf("exec.command", "exec.sha256")),
        row(PayloadTier.Script, actionKeys = listOf("script.pick")),
        row(PayloadTier.Ko, actionKeys = listOf("ko.pick")),
    )
}

fun payloadRunLogLine(draft: PayloadDraft): String = when (draft.tier) {
    null -> "payload: default flow (no custom content)"
    PayloadTier.Exec -> "payload: run as root: " + payloadArgv(draft.execCommand).joinToString(" ")
    PayloadTier.Script -> "payload: run through the LKM: " + (draft.scriptName ?: draft.scriptPath)
    PayloadTier.Ko -> "payload: load " + draft.koEntries.size + " kernel extension(s): " +
        draft.koEntries.joinToString(",") { it.name }
}

/**
 * One label/value line of the payload header. Separate from the plugin page's
 * [PluginHeaderRow]: the payload labels are still raw keys, so the two pages do
 * not force one shape onto the other.
 */
data class PayloadHeaderRow(val label: String, val value: String)

/** Header rows for the payload page. */
fun payloadHeaderRows(draft: PayloadDraft): List<PayloadHeaderRow> = buildList {
    add(PayloadHeaderRow("tier", draft.tier?.token ?: "default"))
    when (draft.tier) {
        PayloadTier.Exec -> {
            add(PayloadHeaderRow("command", draft.execCommand.ifBlank { "-" }))
            add(PayloadHeaderRow("sha256", draft.execSha256 ?: "-"))
        }

        PayloadTier.Script -> {
            add(PayloadHeaderRow("script", draft.scriptName ?: "-"))
            add(PayloadHeaderRow("path", draft.scriptPath ?: "-"))
            add(PayloadHeaderRow("sha256", draft.scriptSha256 ?: "-"))
        }

        PayloadTier.Ko -> {
            add(PayloadHeaderRow("count", draft.koEntries.size.toString()))
            draft.koEntries.forEachIndexed { index, entry ->
                add(PayloadHeaderRow("ko[" + index + "]", entry.name))
            }
        }

        null -> Unit
    }
}
