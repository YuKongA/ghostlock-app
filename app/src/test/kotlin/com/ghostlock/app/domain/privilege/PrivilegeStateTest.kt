package com.ghostlock.app.domain.privilege

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** Covers renderer-neutral executor facts and the three injected privilege use cases. */
class PrivilegeStateTest {
    /** App execution is always authorized and remains the displayed executor. */
    @Test
    fun appExecutorIsAuthorizedWithoutShizuku() {
        val state = state(AttackExecutorKind.APP, available = false, granted = false)
        assertEquals(AttackExecutorState(AttackExecutorKind.APP, true), state.attackExecutor())
        assertEquals(AttackExecutorKind.APP, state.displayedExecutor())
    }

    /** An ungranted Shizuku selection remains exact for the gate but displays the usable app path. */
    @Test
    fun ungrantedShizukuSelectionIsNotAuthorized() {
        val state = state(AttackExecutorKind.SHIZUKU, available = true, granted = false)
        assertFalse(state.attackExecutor().authorized)
        assertEquals(AttackExecutorKind.SHIZUKU, state.attackExecutor().kind)
        assertEquals(AttackExecutorKind.APP, state.displayedExecutor())
    }

    /** A granted Shizuku selection is both authorized and visible. */
    @Test
    fun grantedShizukuSelectionIsAuthorizedAndDisplayed() {
        val state = state(AttackExecutorKind.SHIZUKU, available = true, granted = true)
        assertTrue(state.attackExecutor().authorized)
        assertEquals(AttackExecutorKind.SHIZUKU, state.displayedExecutor())
    }

    /** Use cases preserve the injected port's state identity, refresh command, and exact result. */
    @Test
    fun useCasesDelegateToSingleInjectedPort() = runTest {
        val port = FakePrivilegePort()
        assertSame(port.state, ObservePrivilegeUseCase(port)())

        RefreshPrivilegeUseCase(port)()
        assertEquals(1, port.refreshCount)

        port.selectionResult = ExecutorSelectionResult.PERMISSION_REQUESTED
        assertEquals(
            ExecutorSelectionResult.PERMISSION_REQUESTED,
            SelectPrivilegeExecutorUseCase(port)(AttackExecutorKind.SHIZUKU),
        )
        assertEquals(AttackExecutorKind.SHIZUKU, port.lastSelection)
    }

    /** @return One privilege fixture with stable app uid. */
    private fun state(
        selected: AttackExecutorKind,
        available: Boolean,
        granted: Boolean,
    ): PrivilegeState = PrivilegeState(
        selectedExecutor = selected,
        appUid = 10_123,
        shizukuAvailable = available,
        shizukuGranted = granted,
    )

    /** In-memory port fixture that records every delegated command. */
    private class FakePrivilegePort : PrivilegePort {
        override val state: StateFlow<PrivilegeState> = MutableStateFlow(
            PrivilegeState(
                selectedExecutor = AttackExecutorKind.APP,
                appUid = 10_123,
                shizukuAvailable = false,
                shizukuGranted = false,
            ),
        )
        var refreshCount = 0
        var lastSelection: AttackExecutorKind? = null
        var selectionResult = ExecutorSelectionResult.SELECTED

        override fun refresh() {
            refreshCount += 1
        }

        override suspend fun select(kind: AttackExecutorKind): ExecutorSelectionResult {
            lastSelection = kind
            return selectionResult
        }
    }
}
