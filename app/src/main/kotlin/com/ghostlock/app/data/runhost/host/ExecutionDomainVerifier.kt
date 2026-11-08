package com.ghostlock.app.data.runhost.host

import android.os.Process
import com.ghostlock.app.domain.backend.ExecutionDomain
import java.io.File

/** Physical host role selected by the concrete transport implementation. */
internal enum class RunHostRole {
    APP,
    SHIZUKU_SHELL,
}

/** Verifies immutable BackendDescriptor domain against the actual process UID and SELinux context. */
internal object ExecutionDomainVerifier {
    /**
     * Reads the current process identity and applies the pure domain rule.
     *
     * @param expected Immutable backend execution-domain declaration.
     * @param role Concrete transport host role.
     * @param applicationUid Package UID used by the app-process role.
     * @return True only when role, UID, and SELinux domain all agree.
     */
    fun currentProcessMatches(
        expected: ExecutionDomain,
        role: RunHostRole,
        applicationUid: Int,
    ): Boolean {
        val domain = runCatching {
            File(SELINUX_CONTEXT_PATH).readText().trim().trimEnd('\u0000')
        }.getOrNull() ?: return false
        return matches(
            expected = expected,
            role = role,
            processUid = Process.myUid(),
            applicationUid = applicationUid,
            selinuxDomain = domain,
        )
    }

    /** Pure process-domain rule used by both hosts and host-side contract tests. */
    fun matches(
        expected: ExecutionDomain,
        role: RunHostRole,
        processUid: Int,
        applicationUid: Int,
        selinuxDomain: String,
    ): Boolean {
        val physicalRoleMatches = when (role) {
            RunHostRole.APP ->
                processUid == applicationUid && APP_DOMAIN_PATTERN.matches(selinuxDomain)

            RunHostRole.SHIZUKU_SHELL ->
                processUid == SHELL_UID && selinuxDomain == SHELL_DOMAIN
        }
        if (!physicalRoleMatches) return false
        return when (expected) {
            ExecutionDomain.APP -> role == RunHostRole.APP
            ExecutionDomain.SHIZUKU_SHELL -> role == RunHostRole.SHIZUKU_SHELL
            ExecutionDomain.APP_OR_SHIZUKU -> true
        }
    }

    private val APP_DOMAIN_PATTERN: Regex = Regex(
        "u:r:(?:untrusted_app(?:_[0-9]+)?|platform_app|priv_app):s0(?::.*)?",
    )
    private const val SELINUX_CONTEXT_PATH: String = "/proc/self/attr/current"
    private const val SHELL_UID: Int = 2000
    private const val SHELL_DOMAIN: String = "u:r:shell:s0"
}
