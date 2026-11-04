package com.ghostlock.app.backend.launcher

import com.ghostlock.app.R
import com.ghostlock.app.backend.backends.cve43499.BackendCve43499
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.backends.cve43284glued.BackendCve43284Glued
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.AttackExecutorState
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.BackendUidRequirement
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * uid 需求声明守卫：backend 只声明执行体种类需求；application gate 只按「选择 + Shizuku 授权二值」拒绝，
 * 不读取/解析 Shizuku 的实际 uid。
 */
class BackendUidRequirementTest {
    private fun app() = AttackExecutorState(AttackExecutorKind.APP, authorized = true)
    private fun shizuku(authorized: Boolean = true) =
        AttackExecutorState(AttackExecutorKind.SHIZUKU, authorized)

    @Test
    fun backendsDeclareUidRequirement() {
        assertEquals(BackendUidRequirement.ANY_UID, BackendCve43499.uidRequirement)
        assertTrue(BackendUidRequirement.SHELL_UID_2000.satisfiedBy(AttackExecutorKind.SHIZUKU))
        assertFalse(BackendUidRequirement.SHELL_UID_2000.satisfiedBy(AttackExecutorKind.APP))
        assertTrue(BackendUidRequirement.ANY_UID.satisfiedBy(AttackExecutorKind.APP))
        assertTrue(BackendUidRequirement.ANY_UID.satisfiedBy(AttackExecutorKind.SHIZUKU))
    }

    @Test
    fun anyUidBackendRunsFromBothAttackThreads() {
        for (executor in listOf(app(), shizuku())) {
            val decision = BackendRunGate.decide(
                spec = BackendCve43499Glued,
                readiness = BackendReadiness.Ready(),
                context = RunPageContext(executor = executor),
            )
            assertTrue(executor.toString(), decision.allowed)
        }
    }

    @Test
    fun gluedBackendsRequireAvailableExecutorForNewProcessChannel() {
        for (spec in listOf(BackendCve43499Glued, BackendCve43284Glued)) {
            assertTrue(spec.key, spec.flowSymbol != null)
            val decision = BackendRunGate.decide(
                spec = spec,
                readiness = BackendReadiness.Ready(),
                context = RunPageContext(executor = shizuku(authorized = false)),
            )
            assertFalse(spec.key, decision.allowed)
            assertTrue(
                spec.key,
                decision.reasons.any {
                    it.messageRes == R.string.backend_run_executor_unavailable
                },
            )
        }
    }
}
