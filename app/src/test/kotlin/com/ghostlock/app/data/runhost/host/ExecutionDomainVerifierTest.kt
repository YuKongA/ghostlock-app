package com.ghostlock.app.data.runhost.host

import com.ghostlock.app.domain.backend.ExecutionDomain
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Physical process identity cases for the App and Shizuku RunHost roles. */
class ExecutionDomainVerifierTest {
    /** Concrete host role requires both the expected UID and SELinux domain. */
    @Test
    fun physicalHostIdentityIsChecked() {
        assertTrue(
            ExecutionDomainVerifier.matches(
                expected = ExecutionDomain.APP_OR_SHIZUKU,
                role = RunHostRole.APP,
                processUid = 10_123,
                applicationUid = 10_123,
                selinuxDomain = "u:r:untrusted_app_32:s0:c1,c2",
            ),
        )
        assertTrue(
            ExecutionDomainVerifier.matches(
                expected = ExecutionDomain.SHIZUKU_SHELL,
                role = RunHostRole.SHIZUKU_SHELL,
                processUid = 2000,
                applicationUid = 10_123,
                selinuxDomain = "u:r:shell:s0",
            ),
        )
        assertFalse(
            ExecutionDomainVerifier.matches(
                expected = ExecutionDomain.SHIZUKU_SHELL,
                role = RunHostRole.SHIZUKU_SHELL,
                processUid = 2000,
                applicationUid = 10_123,
                selinuxDomain = "u:r:untrusted_app_32:s0:c1,c2",
            ),
        )
    }
}
