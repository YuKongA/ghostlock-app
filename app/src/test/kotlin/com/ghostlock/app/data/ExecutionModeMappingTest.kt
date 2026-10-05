package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.domain.model.ExecutionMode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * T3d/T5 anchor: the mode plus backend preference must derive the sparse
 * catalogued triple the native selection expects. 43284 only pairs with
 * pagecache_write x umh_forward; 43499 keeps its {w1_w3|w1_w2} x root_child
 * mapping.
 */
class ExecutionModeMappingTest {
    @Test
    fun generalRunsFromAppWithW1W3AndRootChild() {
        val mode = ExecutionMode.General
        assertEquals(ExecutionEntry.App, mode.entry)
        assertEquals(StepSetKind.W1W3, mode.steps)
        assertEquals(FrontendKind.RootChild, mode.terminal)
        assertEquals(BackendKind.Cve2026_43499, mode.backend)
        assertTrue(mode.isAvailable)
        assertFalse(mode.requiresShizuku)
    }

    @Test
    fun shizukuRunsFromShellWithW1W2AndRootChild() {
        val mode = ExecutionMode.Shizuku
        assertEquals(ExecutionEntry.Shell, mode.entry)
        assertEquals(StepSetKind.W1W2, mode.steps)
        assertEquals(FrontendKind.RootChild, mode.terminal)
        assertEquals(BackendKind.Cve2026_43499, mode.backend)
        assertTrue(mode.isAvailable)
        assertTrue(mode.requiresShizuku)
    }

    @Test
    fun umhRunsThe43284PagecacheUmhTriple() {
        val mode = ExecutionMode.Umh
        assertEquals(ExecutionEntry.Shell, mode.entry)
        assertEquals(StepSetKind.PAGE_CACHE_WRITE, mode.steps)
        assertEquals(FrontendKind.UmhForward, mode.terminal)
        assertEquals(BackendKind.Cve2026_43284, mode.backend)
        assertTrue(mode.isAvailable)
        assertFalse(mode.requiresShizuku)
    }

    @Test
    fun onlyShizukuNeedsTheShellAndEveryModeIsAvailable() {
        assertEquals(
            ExecutionMode.entries.toList(),
            ExecutionMode.entries.filter { it.isAvailable },
        )
        assertEquals(
            listOf(ExecutionMode.Shizuku),
            ExecutionMode.entries.filter { it.requiresShizuku },
        )
    }

    @Test
    fun selectingThe43284BackendOverridesTheModeToTheSparseTriple() {
        val selection = resolveExecutionSelection(ExecutionMode.General, BackendKind.Cve2026_43284)
        assertEquals(
            ExecutionSelection(
                backend = BackendKind.Cve2026_43284,
                steps = StepSetKind.PAGE_CACHE_WRITE,
                terminal = FrontendKind.UmhForward,
            ),
            selection,
        )
    }

    @Test
    fun umhModeResolvesTo43284EvenWithTheDefaultBackend() {
        val selection = resolveExecutionSelection(ExecutionMode.Umh, BackendKind.Cve2026_43499)
        assertEquals(BackendKind.Cve2026_43284, selection.backend)
        assertEquals(StepSetKind.PAGE_CACHE_WRITE, selection.steps)
        assertEquals(FrontendKind.UmhForward, selection.terminal)
    }

    @Test
    fun fourThreeFourNineNineKeepsTheModeMapping() {
        assertEquals(
            ExecutionSelection(BackendKind.Cve2026_43499, StepSetKind.W1W3, FrontendKind.RootChild),
            resolveExecutionSelection(ExecutionMode.General, BackendKind.Cve2026_43499),
        )
        assertEquals(
            ExecutionSelection(BackendKind.Cve2026_43499, StepSetKind.W1W2, FrontendKind.RootChild),
            resolveExecutionSelection(ExecutionMode.Shizuku, BackendKind.Cve2026_43499),
        )
    }
}
