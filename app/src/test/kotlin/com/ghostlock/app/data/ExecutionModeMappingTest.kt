package com.ghostlock.app.data

import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.domain.model.ExecutionMode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * T3d anchor: the three-way UI selection must derive the same entry / StepSet /
 * terminal the native catalog expects, and UMH must stay unavailable until the
 * native terminal_available(umh_forward) is true (T5).
 */
class ExecutionModeMappingTest {
    @Test
    fun generalRunsFromAppWithW1W3AndRootChild() {
        val mode = ExecutionMode.General
        assertEquals(ExecutionEntry.App, mode.entry)
        assertEquals(StepSetKind.W1W3, mode.steps)
        assertEquals(FrontendKind.RootChild, mode.terminal)
        assertTrue(mode.isAvailable)
        assertFalse(mode.requiresShizuku)
    }

    @Test
    fun shizukuRunsFromShellWithW1W2AndRootChild() {
        val mode = ExecutionMode.Shizuku
        assertEquals(ExecutionEntry.Shell, mode.entry)
        assertEquals(StepSetKind.W1W2, mode.steps)
        assertEquals(FrontendKind.RootChild, mode.terminal)
        assertTrue(mode.isAvailable)
        assertTrue(mode.requiresShizuku)
    }

    @Test
    fun umhRunsFromShellWithUmhTerminalButIsUnavailable() {
        val mode = ExecutionMode.Umh
        assertEquals(ExecutionEntry.Shell, mode.entry)
        assertEquals(StepSetKind.W1W2, mode.steps)
        assertEquals(FrontendKind.UmhForward, mode.terminal)
        assertFalse(mode.isAvailable)
        assertFalse(mode.requiresShizuku)
    }

    @Test
    fun onlyShizukuNeedsTheShellAndOnlyUmhIsDisabled() {
        assertEquals(
            listOf(ExecutionMode.General, ExecutionMode.Shizuku),
            ExecutionMode.entries.filter { it.isAvailable },
        )
        assertEquals(
            listOf(ExecutionMode.Shizuku),
            ExecutionMode.entries.filter { it.requiresShizuku },
        )
    }
}
