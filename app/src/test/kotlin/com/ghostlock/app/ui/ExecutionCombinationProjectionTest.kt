package com.ghostlock.app.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Guards for the execution-combination projection (design 2.1.1 / 2.1.2 / 2.8).
 *
 * Each test here is the machine check for one design clause; the FAIL-VISIBLE
 * one is deliberately written so that silently hiding an unknown shape fails.
 */
class ExecutionCombinationProjectionTest {
    private val vocabulary = ExecutionComboVocabulary(
        backends = setOf("cve_2026_43499", "cve_2026_43284"),
        routes = setOf("multicast_waiter", "select_stack", "tcp_zerocopy"),
        terminals = setOf("root_child", "shizuku", "umh_forward"),
        steps = setOf("w1", "w2", "w3"),
    )

    private fun entry(
        terminal: String,
        steps: List<String>,
        priority: Int? = null,
        planned: Boolean = false,
        supported: Boolean = true,
        route: String? = "multicast_waiter",
    ) = ExecutionComboEntry(
        backend = "cve_2026_43499",
        route = route,
        steps = steps,
        terminal = terminal,
        priority = priority,
        planned = planned,
        supported = supported,
    )

    @Test
    fun sameBackendRouteEntriesBecomeSiblingLeaves() {
        val tree = executionComboTree(
            listOf(
                entry("root_child", listOf("w1", "w2", "w3"), priority = 2),
                entry("shizuku", listOf("w1", "w2"), priority = 3),
                entry("umh_forward", listOf("w1", "w2"), priority = 4),
            ),
            vocabulary,
        )
        assertEquals("one (backend, route) node", 1, tree.groups.size)
        assertEquals("three sibling leaves", 3, tree.groups.single().leaves.size)
        assertTrue(tree.failures.isEmpty())
    }

    @Test
    fun leavesAreOrderedByPriorityNotByInputOrder() {
        val declared = listOf(
            entry("root_child", listOf("w1", "w2", "w3"), priority = 2),
            entry("shizuku", listOf("w1", "w2"), priority = 3),
            entry("umh_forward", listOf("w1", "w2"), priority = 4),
        )
        val shuffled = listOf(declared[2], declared[0], declared[1])
        val forward = executionComboTree(declared, vocabulary).groups.single().leaves
        val backward = executionComboTree(shuffled, vocabulary).groups.single().leaves
        assertEquals(
            "shuffling the input must not change the menu",
            forward.map { it.entry.terminal },
            backward.map { it.entry.terminal },
        )
        assertEquals(listOf("root_child", "shizuku", "umh_forward"), forward.map { it.entry.terminal })
    }

    @Test
    fun equalPrioritiesUseADeterministicTieBreak() {
        /* Both entries have no declared priority: the tie-break (steps, then
         * terminal) decides, so the menu is stable without a priority field. */
        val first = entry("shizuku", listOf("w1", "w2"))
        val second = entry("root_child", listOf("w1", "w2", "w3"))
        val leaves = executionComboTree(listOf(second, first), vocabulary)
            .groups.single().leaves
        assertEquals(listOf("shizuku", "root_child"), leaves.map { it.entry.terminal })
        assertNull("no priority is invented", leaves.first().entry.priority)
    }

    @Test
    fun unknownShapesAreFailVisible() {
        val unknownBackend = entry("root_child", listOf("w1")).copy(backend = "cve_2099_00001")
        val unknownRoute = entry("root_child", listOf("w1"), route = "not_a_route")
        val unknownTerminal = entry("not_a_terminal", listOf("w1"))
        val unknownStep = entry("root_child", listOf("w1", "w9"))
        val noSteps = entry("root_child", emptyList())
        val tree = executionComboTree(
            listOf(unknownBackend, unknownRoute, unknownTerminal, unknownStep, noSteps),
            vocabulary,
        )
        /* Fail-visible: every unknown shape is reported, none is hidden. */
        assertTrue(tree.failures.any { it.contains("unknown backend") })
        assertTrue(tree.failures.any { it.contains("unknown route") })
        assertTrue(tree.failures.any { it.contains("unknown terminal") })
        assertTrue(tree.failures.any { it.contains("unknown step") })
        assertTrue(tree.failures.any { it.contains("empty step sequence") })
        assertEquals(5, tree.failures.size)
    }

    @Test
    fun aBackendWithoutRouteAxisKeepsItsOwnGroup() {
        val routeLess = ExecutionComboEntry(
            backend = "cve_2026_43284",
            route = null,
            steps = listOf("w1"),
            terminal = "umh_forward",
            priority = 1,
        )
        val tree = executionComboTree(listOf(routeLess), vocabulary)
        assertTrue(tree.failures.isEmpty())
        assertNull("no route axis: the group degrades to the backend itself", tree.groups.single().route)
        assertEquals("cve_2026_43284", tree.groups.single().backend)
    }

    @Test
    fun plannedAndUnsupportedAreDistinctAndBothUnselectable() {
        val planned = entry("umh_forward", listOf("w1", "w2"), priority = 4, planned = true)
        val unsupported = entry("shizuku", listOf("w1", "w2"), priority = 5, supported = false)
        val leaves = executionComboTree(listOf(planned, unsupported), vocabulary)
            .groups.single().leaves
        assertEquals(ExecutionComboNote.PLANNED, leaves.first { it.entry.terminal == "umh_forward" }.note)
        assertEquals(
            ExecutionComboNote.UNSUPPORTED,
            leaves.first { it.entry.terminal == "shizuku" }.note,
        )
        assertFalse(leaves.any { it.selectable })
    }

    @Test
    fun theDefaultEntryIsTheFirstSelectableOne() {
        val tree = executionComboTree(
            listOf(
                entry("umh_forward", listOf("w1"), priority = 1, planned = true),
                entry("root_child", listOf("w1", "w2"), priority = 2),
            ),
            vocabulary,
        )
        assertEquals("root_child", executionComboDefaultEntry(tree)?.terminal)
    }
}
