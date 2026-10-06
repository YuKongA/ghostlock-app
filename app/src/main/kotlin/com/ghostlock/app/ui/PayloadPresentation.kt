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
)

/**
 * The payload as the App holds it (in-memory: persistence lands with the wire
 * owner, so nothing half-wired is written into the profile yet).
 *
 * `tier == null` is the DEFAULT choice, not a missing value: it means "nothing
 * custom", so no `payload` section is emitted. It is the initial selection. The
 * other three tiers keep their values while inactive, exactly like the plugin run
 * selection keeps "installed" and "loaded this run" apart.
 *
 * There is NO hash field here by the user's ruling ("assume the user knows what
 * they passed in"): the page never asks for one. The wire contract still carries
 * `*.sha256` — native verifies it when present, accepts when absent — it is only
 * this page that no longer emits it.
 */
data class PayloadDraft(
    val tier: PayloadTier? = null,
    /**
     * The root manager to launch, chosen in the default tier's submenu. null =
     * the SYSTEM DEFAULT (KernelSU as the device ships it), which is also the
     * behaviour of every run that does not pick one.
     */
    val rootManager: RootManager? = null,
    /** argv semantics: split on whitespace, NO shell. */
    val execCommand: String = "",
    val scriptName: String? = null,
    val scriptPath: String? = null,
    val koEntries: List<PayloadKoEntry> = emptyList(),
) {
    val active: Boolean
        get() = when (tier) {
            PayloadTier.Exec -> execCommand.isNotBlank()
            PayloadTier.Script -> scriptPath != null
            PayloadTier.Ko -> koEntries.isNotEmpty()
            null -> false
        }
}

/** Maximum number of ko modules native accepts. */
const val PAYLOAD_MAX_KO = 8

/*
 * COMMENTED OUT (user ruling 2026-10-05): the custom-execution (payload) wire
 * values are withdrawn while that design is redone. The projections above (tier
 * rows, blockers, manager picker, log line) still describe the page; nothing is
 * written to the document.
 *
 * Restore = uncomment this function and, when batch (b) lands, its call site.
 *
 * The payload's profile values (`payload.*`). ONLY the selected tier appears,
 * so switching tiers is the switch: nothing has to be deleted to disable one.
 * Values are spelled exactly as the frozen contract names them. `*.sha256` is
 * NOT emitted: the page no longer asks for a hash (the wire field stays in the
 * contract for anyone who writes it by hand).
 */
// fun payloadValues(draft: PayloadDraft): Map<String, PluginValue> {
//     val values = linkedMapOf<String, PluginValue>()
//     val tier = draft.tier ?: return values
//     values["tier"] = PluginValue.Str(tier.token)
//     when (tier) {
//         PayloadTier.Exec -> {
//             values["exec.command"] = PluginValue.Str(draft.execCommand)
//         }
//
//         PayloadTier.Script -> {
//             draft.scriptPath?.let { values["script.path"] = PluginValue.Str(it) }
//         }
//
//         PayloadTier.Ko -> {
//             values["ko.count"] = PluginValue.UInt(draft.koEntries.size.toULong())
//             draft.koEntries.forEachIndexed { index, entry ->
//                 values["ko." + index + ".path"] = PluginValue.Str(entry.path)
//             }
//         }
//     }
//     return values
// }

/**
 * One row of the default tier's manager picker. [manager] null = the system
 * default, which is the first row and the initial selection.
 */
data class RootManagerRow(
    val manager: RootManager?,
    val selected: Boolean,
    /** The package this row would launch. */
    val packageName: String,
)

/**
 * The manager rows for the default tier: the SYSTEM DEFAULT first, then every
 * SUPPORTED manager that is actually installed and launchable ([installed] comes
 * from `packageManager.getLaunchIntentForPackage(pkg) != null` — the same check
 * the launch itself does, so a row can never offer a target that cannot open).
 *
 * A manager that is not installed is NOT listed: the user asked that no manager
 * be offered unless it exists and can start. The default row is always there.
 */
fun payloadManagerRows(draft: PayloadDraft, installed: Set<String>): List<RootManagerRow> = buildList {
    add(RootManagerRow(null, draft.rootManager == null, RootManager.KernelSU.packageName))
    for (manager in RootManager.entries) {
        if (manager.packageName !in installed) continue
        add(RootManagerRow(manager, draft.rootManager == manager, manager.packageName))
    }
}

/**
 * The manager this run must launch, or null when the choice stays the run's own
 * default (the combination's terminal decides). Only the default tier carries a
 * picker: the other tiers do not touch the manager.
 */
fun payloadLaunchManager(draft: PayloadDraft): RootManager? =
    if (draft.tier == null) draft.rootManager else null

/**
 * Why this draft cannot run yet, in the user's own words; EMPTY means ready.
 *
 * The page no longer carries a check list: a blocker is stated where it is
 * actionable — next to the run button, and as a toast when the user taps run
 * (the same treatment the plugin selection gets). The inline status lines reuse
 * the same wording, so the page and the gate never disagree.
 */
fun payloadBlockers(draft: PayloadDraft): List<PluginLine> = when (draft.tier) {
    null -> emptyList()
    PayloadTier.Exec -> buildList {
        if (payloadArgv(draft.execCommand).isEmpty()) {
            add(PluginLine(R.string.payload_block_command_empty))
        }
    }

    PayloadTier.Script -> buildList {
        if (draft.scriptPath == null) add(PluginLine(R.string.payload_script_none))
    }

    PayloadTier.Ko -> buildList {
        if (draft.koEntries.isEmpty()) add(PluginLine(R.string.payload_ko_none))
        if (draft.koEntries.size > PAYLOAD_MAX_KO) {
            add(PluginLine(R.string.payload_block_ko_too_many, listOf(PAYLOAD_MAX_KO.toString())))
        }
    }
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
        row(PayloadTier.Exec, fieldKeys = listOf("exec.command")),
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
        }

        PayloadTier.Script -> {
            add(PayloadHeaderRow("script", draft.scriptName ?: "-"))
            add(PayloadHeaderRow("path", draft.scriptPath ?: "-"))
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
