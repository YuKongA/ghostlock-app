package com.ghostlock.app.ui

import com.ghostlock.app.data.DeclaredCombination
import com.ghostlock.app.data.StepSetKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.component.stepNames

/**
 * UI projection for the execution-combination submenu (design
 * `docs/analysis/execution-combination-menu-and-general-profile.md` 2.1.1 / 2.1.2).
 *
 * The profile declares a FLAT registry: one entry = one complete path
 * (backend + route + queue/steps + terminal + priority). The MENU TREE IS
 * DERIVED HERE, never configured: level 1 = backend, level 2 = route, leaves =
 * the step sequence + terminal. This file is a pure projection (no Compose) so
 * the derivation is unit-testable.
 *
 * DATA SOURCE CAVEAT (must stay visible, design 2.8): the current caller feeds
 * this projection from the combination catalogue, whose order is NOT the
 * profile declaration order. The design says the declared order IS the priority
 * and the default is the first declared entry, so until the data layer exposes
 * the declared entries the DEFAULT IS NOT GUARANTEED to be the first declared
 * one. [executionComboDefaultEntry] therefore falls back to the first entry it
 * is given and the UI must label that limitation. When the declared entries
 * arrive, only the INPUT changes -- this logic stays as is.
 *
 * Unknown shapes are FAIL-VISIBLE (design 2.1.2): anything the vocabulary does
 * not know becomes an entry in [ExecutionComboTree.failures] (rendered red),
 * never silently hidden.
 */
data class ExecutionComboEntry(
    /** Catalogue token this entry came from; empty when unknown. */
    val token: String = "",
    val backend: String,
    /** null = this backend has no route axis (for example cve_2026_43284). */
    val route: String?,
    val steps: List<String>,
    val terminal: String,
    /** Declared priority; null = the source has none (deterministic fallback). */
    val priority: Int?,
    /** Declared but not implemented yet: visible, NOT selectable (design 2.1.1). */
    val planned: Boolean = false,
    /** Known but not supported by this build: visible, NOT selectable. */
    val supported: Boolean = true,
)

/** Why a leaf cannot be selected. PLANNED and UNSUPPORTED are distinct. */
enum class ExecutionComboNote { PLANNED, UNSUPPORTED }

data class ExecutionComboLeaf(
    val entry: ExecutionComboEntry,
    val selectable: Boolean,
    val note: ExecutionComboNote?,
)

/** One (backend, route) node of the derived tree; route == null = no route axis. */
data class ExecutionComboGroup(
    val backend: String,
    val route: String?,
    val leaves: List<ExecutionComboLeaf>,
)

data class ExecutionComboTree(
    val groups: List<ExecutionComboGroup>,
    /** Fail-visible problems: unknown backend/route/terminal/step or no steps. */
    val failures: List<String>,
)

/** The vocabulary the projection validates against (from the manifests). */
data class ExecutionComboVocabulary(
    val backends: Set<String>,
    val routes: Set<String>,
    val terminals: Set<String>,
    val steps: Set<String>,
)

/**
 * Derives the tree from flat entries.
 *
 * Ordering: leaves inside a group are sorted by priority ascending, then by a
 * deterministic tie-break (step sequence, then terminal), so shuffling the
 * INPUT no longer changes the menu. Groups keep first-appearance order.
 */
fun executionComboTree(
    entries: List<ExecutionComboEntry>,
    vocabulary: ExecutionComboVocabulary,
): ExecutionComboTree {
    val failures = mutableListOf<String>()
    val order = mutableListOf<Pair<String, String?>>()
    val grouped = mutableMapOf<Pair<String, String?>, MutableList<ExecutionComboLeaf>>()
    for (entry in entries) {
        val id = entry.backend + pathSuffix(entry)
        if (entry.backend !in vocabulary.backends) {
            failures += "execution-combo: unknown backend: " + entry.backend
        }
        if (entry.route != null && entry.route !in vocabulary.routes) {
            failures += "execution-combo: unknown route: " + entry.route + " (" + id + ")"
        }
        if (entry.terminal !in vocabulary.terminals) {
            failures += "execution-combo: unknown terminal: " + entry.terminal + " (" + id + ")"
        }
        if (entry.steps.isEmpty()) {
            failures += "execution-combo: empty step sequence (" + id + ")"
        }
        for (step in entry.steps) {
            if (step !in vocabulary.steps) {
                failures += "execution-combo: unknown step: " + step + " (" + id + ")"
            }
        }
        val note = when {
            entry.planned -> ExecutionComboNote.PLANNED
            !entry.supported -> ExecutionComboNote.UNSUPPORTED
            else -> null
        }
        val key = entry.backend to entry.route
        if (key !in grouped) {
            grouped[key] = mutableListOf()
            order += key
        }
        grouped.getValue(key) += ExecutionComboLeaf(
            entry = entry,
            selectable = note == null,
            note = note,
        )
    }
    val groups = order.map { key ->
        ExecutionComboGroup(
            backend = key.first,
            route = key.second,
            leaves = grouped.getValue(key).sortedWith(leafOrder),
        )
    }
    return ExecutionComboTree(groups = groups, failures = failures)
}

/** priority ascending, then step sequence, then terminal: deterministic. */
private val leafOrder = Comparator<ExecutionComboLeaf> { left, right ->
    val byPriority = (left.entry.priority ?: Int.MAX_VALUE)
        .compareTo(right.entry.priority ?: Int.MAX_VALUE)
    if (byPriority != 0) return@Comparator byPriority
    val bySteps = left.entry.steps.joinToString(">").compareTo(right.entry.steps.joinToString(">"))
    if (bySteps != 0) return@Comparator bySteps
    left.entry.terminal.compareTo(right.entry.terminal)
}

private fun pathSuffix(entry: ExecutionComboEntry): String {
    val route = entry.route?.let { "/" + it } ?: ""
    return route + " [" + entry.steps.joinToString(">") + " -> " + entry.terminal + "]"
}

/**
 * The default selection when the user has not chosen yet (design 2.8).
 *
 * The design says "first DECLARED entry". The current source cannot promise
 * that order, so this returns the first SELECTABLE entry of the given list and
 * the UI must label the limitation. Returns null when nothing is selectable.
 */
fun executionComboDefaultEntry(tree: ExecutionComboTree): ExecutionComboEntry? =
    tree.groups.asSequence()
        .flatMap { it.leaves.asSequence() }
        .firstOrNull { it.selectable }
        ?.entry

/**
 * Catalogue -> flat entries for the menu tree.
 *
 * The step sequence comes from StepSetKind.stepNames() (the native-exported
 * stepset-steps.tsv), never from a hand-copied list. A stepset whose row is
 * missing makes that call fail HARD; the failure is carried into the visible
 * tree failures instead of being swallowed, so the menu cannot silently
 * shrink (design 2.1.2 fail-visible).
 */
fun executionComboEntries(
    declared: List<DeclaredCombination>,
): Pair<List<ExecutionComboEntry>, List<String>> {
    val entries = mutableListOf<ExecutionComboEntry>()
    val failures = mutableListOf<String>()
    for (path in declared) {
        /* The DECLARATION decides which paths exist (design 2.9/U17): a path the
         * profile does not declare is not listed at all - not greyed out. The
         * catalogue stays the authority for the token, the terminal and the
         * wiring availability, so `planned`/`supported` keep their old meaning
         * (a declared path with no catalogue row is not wired: planned). */
        val spec = CombinationCatalog.specs.firstOrNull { candidate ->
            candidate.backend.token == path.backend &&
                candidate.route?.token == path.route &&
                runCatching { candidate.steps.stepNames() }.getOrNull() == path.steps
        }
        if (spec == null) {
            failures += "execution-combo: declared path has no catalogue row: " +
                path.backend + (path.route?.let { "/" + it } ?: "")
        }
        entries += ExecutionComboEntry(
            token = spec?.token ?: "",
            backend = path.backend,
            route = path.route,
            steps = path.steps,
            /* TOKEN, not the enum name: the vocabulary and the declared side both
             * speak tokens, so the enum name showed up as unknown. */
            terminal = path.handoff ?: spec?.terminal?.token ?: "",
            priority = path.priority?.toInt(),
            planned = spec == null || !spec.available,
            supported = spec?.available == true,
        )
    }
    return entries to failures
}

/** Vocabulary the projection validates against, from the catalogue. */
fun executionComboVocabulary(): ExecutionComboVocabulary {
    val options = combinationOptions()
    return ExecutionComboVocabulary(
        /* TOKENS, not enum names: the declared side speaks tokens (cve_2026_43284),
         * so comparing against enum names made every declared backend unknown. */
        backends = options.map { it.spec.backend.token }.toSet(),
        routes = options.mapNotNull { it.spec.route?.token }.toSet(),
        terminals = options.map { it.spec.terminal.token }.toSet(),
        steps = options.flatMap { option ->
            try {
                option.spec.steps.stepNames()
            } catch (error: IllegalArgumentException) {
                emptyList()
            }
        }.toSet(),
    )
}

/**
 * The catalogue row of the DECLARED default path (design 2.9/U17): the first
 * declared combination ([ProfileLayout.declaredCombinations] is already in
 * priority order) joined against the catalogue, or null when the profile
 * declares nothing.
 *
 * HISTORY (2026-10-06, user complaint "undeclared paths are selectable"): the
 * UI used to start on CombinationCatalog.defaultSpec - a catalogue default that
 * can contradict the declaration (a profile declaring 43284 first emitted a
 * 43499 document, so the wire carried a route the declared default cannot have).
 * The declaration now decides the default; the catalogue default is only the
 * fallback when the profile declares nothing.
 */
fun declaredDefaultCombination(
    declared: List<DeclaredCombination>,
): CombinationSpec? {
    val first = declared.firstOrNull() ?: return null
    return CombinationCatalog.specs.firstOrNull { candidate ->
        candidate.backend.token == first.backend &&
            candidate.route?.token == first.route &&
            runCatching { candidate.steps.stepNames() }.getOrNull() == first.steps
    }
}

/** Derived menu tree for the sub-page, including the visible failures. */
fun GhostlockUiState.executionComboTree(): ExecutionComboTree {
    val (entries, failures) = executionComboEntries(declaredCombinations)
    val tree = executionComboTree(entries, executionComboVocabulary())
    return tree.copy(failures = tree.failures + failures)
}
