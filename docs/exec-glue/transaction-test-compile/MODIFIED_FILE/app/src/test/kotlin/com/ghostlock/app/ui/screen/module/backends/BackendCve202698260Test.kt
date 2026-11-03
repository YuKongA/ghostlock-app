package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.R
import com.ghostlock.app.backend.BackendId
import com.ghostlock.app.backend.declarations.BackendCustom
import com.ghostlock.app.backend.declarations.BackendCve202643284
import com.ghostlock.app.backend.declarations.BackendCve202643499
import com.ghostlock.app.backend.declarations.BackendCve202698260
import com.ghostlock.app.backend.declarations.Backends
import com.ghostlock.app.backend.declarations.cve43499glued.Backend43499Glued
import com.ghostlock.app.backend.declarations.cve43284glued.Backend43284Glued
import com.ghostlock.app.backend.cve98260.flow.Flow98260Impl
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.BackendUidRequirement
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * B3 注册面单测：98260 在 UI 注册表中的身份/顺序/能力/就绪 fail-closed。
 *
 * 纯 JVM；不触 Shizuku/真机（探针未 attach ⇒ NotReady 是预期 fail-closed）。
 */
class BackendCve202698260Test {
    @Test
    fun registryOrderAndIdentityAreLocked() {
        assertEquals(
            listOf(
                Backend43499Glued.key, Backend43284Glued.key, BackendCve202643284.key,
                BackendCve202698260.key, BackendCve202643499.key, BackendCustom.key,
            ),
            Backends.all.map { it.key },
        )
        assertEquals("CVE-2026-98260", BackendCve202698260.cve)
        assertEquals(BackendCve202698260.key, Backends.all[3].key)
        assertEquals(BackendId.CVE_98260, BackendId.CVE_98260)
    }

    @Test
    fun backendIdBindsTheFlowClass() {
        assertEquals(Flow98260Impl::class, BackendId.CVE_98260.clazz)
        assertEquals(Flow98260Impl::class, BackendCve202698260.id?.clazz)
    }

    @Test
    fun specIsDisabledWithoutRunnerAndNotReadyByDefault() {
        assertEquals(BackendRunCapability.NO_FLOW_RUN, BackendCve202698260.runCapability)
        assertFalse(BackendCve202698260.selectable)
        assertFalse(BackendCve202698260.abortable)
        assertEquals(R.string.backend_cve_2026_98260, BackendCve202698260.titleRes)
        val readiness = BackendCve202698260.readiness()
        assertTrue(readiness is BackendReadiness.NotReady)
        assertTrue((readiness as BackendReadiness.NotReady).reasons.isNotEmpty())
        assertFalse(readiness.isReady)
    }

    @Test
    fun uidRequirementIsDeclaredForUiCheckOnly() {
        /* backend 只声明 uid 需求；是否满足由 UI 启动前检查（backend 不自检）。 */
        assertEquals(BackendUidRequirement.SHELL_UID_2000, BackendCve202698260.uidRequirement)
        assertEquals(BackendUidRequirement.ANY_UID, BackendCustom.uidRequirement)
    }

    @Test
    fun readinessOnlyMapsInputFacts() {
        val readiness = BackendCve202698260.declare(
            listOf(
                "cve98260:preflight:profile-missing",
                "cve98260:preflight:payload-missing",
                "cve98260:preflight:release-mismatch",
                "cve98260:preflight:must-reboot",
                "cve98260:preflight:boot-id-unknown",
            ),
            ready = false,
        )
        assertTrue(readiness is BackendReadiness.NotReady)
        val messages = (readiness as BackendReadiness.NotReady).reasons.map { it.messageRes }
        assertTrue(messages.contains(R.string.backend_readiness_cve98260_profile_missing))
        assertTrue(messages.contains(R.string.backend_readiness_cve98260_payload))
        assertTrue(messages.contains(R.string.backend_readiness_cve98260_profile_release))
        assertTrue(messages.contains(R.string.backend_readiness_cve98260_must_reboot))
        assertTrue(messages.contains(R.string.backend_readiness_cve98260_boot_id))
    }

    @Test
    fun readyDeclarationIsPlainReady() {
        val readiness = BackendCve202698260.declare(emptyList(), ready = true)
        assertTrue(readiness is BackendReadiness.Ready)
        assertTrue((readiness as BackendReadiness.Ready).notes.isEmpty())
    }

    @Test
    fun customBackendHasNoIdentityAuthority() {
        assertNull(BackendCustom.id)
        assertTrue(BackendCustom.abortable)
        assertEquals("CUSTOM", BackendCustom.cve)
    }
}
