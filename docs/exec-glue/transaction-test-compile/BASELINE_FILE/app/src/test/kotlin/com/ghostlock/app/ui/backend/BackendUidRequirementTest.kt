package com.ghostlock.app.ui.backend

import com.ghostlock.app.R
import com.ghostlock.app.ui.screen.module.backends.BackendCve202643499
import com.ghostlock.app.ui.screen.module.backends.BackendCve202698260
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * uid 需求声明守卫：backend 只声明执行体种类需求；UI 只按「选择 + Shizuku 授权二值」拒绝，
 * 不读取/解析 Shizuku 的实际 uid。
 */
class BackendUidRequirementTest {
    private fun app() = AttackExecutorState(AttackExecutorKind.APP, authorized = true)
    private fun shizuku(authorized: Boolean = true) =
        AttackExecutorState(AttackExecutorKind.SHIZUKU, authorized)

    @Test
    fun backendsDeclareUidRequirement() {
        assertEquals(BackendUidRequirement.SHELL_UID_2000, BackendCve202698260.uidRequirement)
        assertEquals(BackendUidRequirement.ANY_UID, BackendCve202643499.uidRequirement)
        assertTrue(BackendUidRequirement.SHELL_UID_2000.satisfiedBy(AttackExecutorKind.SHIZUKU))
        assertFalse(BackendUidRequirement.SHELL_UID_2000.satisfiedBy(AttackExecutorKind.APP))
        assertTrue(BackendUidRequirement.ANY_UID.satisfiedBy(AttackExecutorKind.APP))
        assertTrue(BackendUidRequirement.ANY_UID.satisfiedBy(AttackExecutorKind.SHIZUKU))
    }

    @Test
    fun uiRejectsShellBackendOnAppAttackThread() {
        val decision = BackendRunGate.decide(
            spec = BackendCve202698260,
            readiness = BackendReadiness.Ready(),
            context = RunPageContext(executor = app()),
        )
        assertFalse(decision.allowed)
        assertTrue(decision.reasons.any { it.messageRes == R.string.backend_run_uid_requirement })
    }

    @Test
    fun uiRejectsWhenShizukuNotAuthorized() {
        val decision = BackendRunGate.decide(
            spec = BackendCve202698260,
            readiness = BackendReadiness.Ready(),
            context = RunPageContext(executor = shizuku(authorized = false)),
        )
        assertFalse(decision.allowed)
        assertTrue(decision.reasons.any { it.messageRes == R.string.backend_run_executor_unavailable })
    }

    @Test
    fun anyUidBackendRunsFromBothAttackThreads() {
        for (executor in listOf(app(), shizuku())) {
            val decision = BackendRunGate.decide(
                spec = BackendCve202643499,
                readiness = BackendReadiness.Ready(),
                context = RunPageContext(executor = executor),
            )
            assertTrue(executor.toString(), decision.allowed)
        }
    }

    @Test
    fun shellBackendAllowedWhenShizukuAuthorized() {
        val decision = BackendRunGate.decide(
            spec = BackendCve202698260,
            readiness = BackendReadiness.Ready(),
            context = RunPageContext(executor = shizuku(authorized = true)),
        )
        assertTrue(decision.allowed)
    }
}
