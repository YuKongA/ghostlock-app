package com.ghostlock.app.core.run

import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.AttackExecutorState
import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Neutral execution-domain gate cases without concrete backend dependencies. */
class RunAdmissionPolicyTest {
    /** App-only specs reject a selected shell executor. */
    @Test
    fun appDomainRejectsShellExecutor() {
        val decision = RunAdmissionPolicy.decide(
            spec(ExecutionDomain.APP),
            BackendAvailability.Ready(),
            RunAdmissionContext(
                executor = AttackExecutorState(AttackExecutorKind.SHIZUKU, true),
                shizukuTypedHostAvailable = true,
            ),
        )
        assertFalse(decision.allowed)
    }

    /** Shell specs require authorization and the installed typed host adapter. */
    @Test
    fun shellDomainRequiresAuthorizedTypedHost() {
        val candidate = spec(ExecutionDomain.SHIZUKU_SHELL)
        assertFalse(
            RunAdmissionPolicy.decide(
                candidate,
                BackendAvailability.Ready(),
                RunAdmissionContext(
                    executor = AttackExecutorState(AttackExecutorKind.SHIZUKU, false),
                    shizukuTypedHostAvailable = true,
                ),
            ).allowed,
        )
        assertTrue(
            RunAdmissionPolicy.decide(
                candidate,
                BackendAvailability.Ready(),
                RunAdmissionContext(
                    executor = AttackExecutorState(AttackExecutorKind.SHIZUKU, true),
                    shizukuTypedHostAvailable = true,
                ),
            ).allowed,
        )
    }

    private fun spec(domain: ExecutionDomain): BackendDescriptor = BackendDescriptor(
        key = "TEST-${domain.name}",
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = domain,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )
}
