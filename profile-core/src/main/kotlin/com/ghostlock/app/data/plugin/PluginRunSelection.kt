package com.ghostlock.app.data.plugin

/**
 * Run-level plugin selection (approved design, batch 1).
 *
 * Three layers of meaning, deliberately separate:
 *  - the registry row = the module is installed and hash-pinned;
 *  - the persisted `enabled` flag = the user trusts it and wants it to take
 *    part in runs;
 *  - the run selection = which of those THIS execution actually loads.
 *
 * The selection is transient: `null` means "the default", i.e. every enabled
 * plugin, so an untouched App keeps composing byte-identical documents. An
 * EMPTY set is meaningful and allowed: this run loads no plugin at all, and
 * then the document simply has no `plugin` section.
 *
 * A plugin that is selected but cannot be described still fails the document
 * build (the user asked for it); a plugin that is NOT selected can never block
 * a run.
 */
/**
 * The resolved run selection as the DOCUMENT BUILD sees it: what to emit, or why
 * it cannot emit anything.
 *
 * A failure is a VALUE, never an exception. The build runs on every profile load
 * — a hot path — and the old "throw when a selected plugin has no descriptor"
 * crashed the whole process there. [Blocked] carries the probe's own reasons so
 * the page and the run gate can show them. Only SELECTED plugins can block: an
 * enabled plugin this run does not load is simply not resolved at all.
 */
sealed interface PluginSelection {
    /** The plugins to emit, in registry order. */
    data class Ready(val plugins: List<EnabledPlugin>) : PluginSelection

    /** At least one SELECTED plugin could not be described; [reasons] say why. */
    data class Blocked(val reasons: List<String>) : PluginSelection
}

object PluginRunSelection {
    /** The plugins this run loads, in registry order. */
    fun of(
        entries: List<PluginManifestEntry>,
        selection: Set<String>?,
    ): List<PluginManifestEntry> =
        entries.filter { it.enabled && (selection == null || it.id in selection) }

    /** `(selected, enabled)` counts for the pre-run summary. */
    fun counts(entries: List<PluginManifestEntry>, selection: Set<String>?): Pair<Int, Int> {
        val enabled = entries.count { it.enabled }
        return of(entries, selection).size to enabled
    }

    /**
     * The line the run log records (never the wire): what this run will load,
     * and what stayed behind because the user unselected it.
     */
    fun logLine(entries: List<PluginManifestEntry>, selection: Set<String>?): String {
        val selected = of(entries, selection)
        val skipped = entries.filter { it.enabled && it !in selected }
        val loaded = if (selected.isEmpty()) {
            "none"
        } else {
            selected.joinToString(",") { it.id + "@" + it.version }
        }
        val suffix = if (skipped.isEmpty()) {
            ""
        } else {
            "; enabled but not selected: " + skipped.joinToString(",") { it.id }
        }
        return "plugins: loading " + loaded + suffix
    }
}
