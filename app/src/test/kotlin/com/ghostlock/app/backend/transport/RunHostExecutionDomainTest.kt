package com.ghostlock.app.backend.transport

import com.ghostlock.app.backend.launcher.BackendRunGate
import com.ghostlock.app.backend.launcher.RunPageContext
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendConfigurationSpec
import com.ghostlock.app.domain.backend.BackendParameterRequirements
import com.ghostlock.app.domain.backend.BackendPresentationIdentity
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.AttackExecutorState
import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Neutral execution-domain gate cases without concrete backend dependencies. */
class RunHostExecutionDomainTest {
    /** App-only specs reject a selected shell executor. */
    @Test
    fun appDomainRejectsShellExecutor() {
        val decision = BackendRunGate.decide(
            spec(ExecutionDomain.APP),
            BackendAvailability.Ready(),
            RunPageContext(
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
            BackendRunGate.decide(
                candidate,
                BackendAvailability.Ready(),
                RunPageContext(
                    executor = AttackExecutorState(AttackExecutorKind.SHIZUKU, false),
                    shizukuTypedHostAvailable = true,
                ),
            ).allowed,
        )
        assertTrue(
            BackendRunGate.decide(
                candidate,
                BackendAvailability.Ready(),
                RunPageContext(
                    executor = AttackExecutorState(AttackExecutorKind.SHIZUKU, true),
                    shizukuTypedHostAvailable = true,
                ),
            ).allowed,
        )
    }

    /** Concrete host role also requires the actual UID and SELinux domain to agree. */
    @Test
    fun physicalHostIdentityIsChecked() {
        assertTrue(
            RunHostExecutionDomainVerifier.matches(
                expected = ExecutionDomain.APP_OR_SHIZUKU,
                role = RunHostRole.APP,
                processUid = 10_123,
                applicationUid = 10_123,
                selinuxDomain = "u:r:untrusted_app_32:s0:c1,c2",
            ),
        )
        assertTrue(
            RunHostExecutionDomainVerifier.matches(
                expected = ExecutionDomain.SHIZUKU_SHELL,
                role = RunHostRole.SHIZUKU_SHELL,
                processUid = 2000,
                applicationUid = 10_123,
                selinuxDomain = "u:r:shell:s0",
            ),
        )
        assertFalse(
            RunHostExecutionDomainVerifier.matches(
                expected = ExecutionDomain.SHIZUKU_SHELL,
                role = RunHostRole.SHIZUKU_SHELL,
                processUid = 2000,
                applicationUid = 10_123,
                selinuxDomain = "u:r:untrusted_app_32:s0:c1,c2",
            ),
        )
    }

    private fun spec(domain: ExecutionDomain): BackendSpec = BackendSpec(
        key = "TEST-${domain.name}",
        presentation = BackendPresentationIdentity("Test", domain.name),
        selectionPolicy = BackendSelectionPolicy.SELECTABLE,
        abortPolicy = AbortPolicy.SAFE_DURING_RUN,
        executionDomain = domain,
        configuration = BackendConfigurationSpec(),
        parameterRequirements = BackendParameterRequirements.EMPTY,
        runCapability = RunCapability.DIAGNOSTIC_ONLY,
    )
}
