package com.ghostlock.app.backend.backends.cve43499.victim

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-91 A1 killPolicy 单测（先写不运行）。
 *
 * 覆盖：默认 43499 语义（killOnTimeout=false/rooted close 不杀/非 rooted 可回收/detachOnTimeout=true）、
 * rooted 分流、超时不 kill victim、factory/runtime 携带 policy。
 */
class Cve43499VictimKillPolicyTest {

    @Test
    fun `default policy matches 43499 a1 semantics`() {
        val policy = Cve43499VictimKillPolicy.DEFAULT
        assertFalse(policy.killOnTimeout)
        assertFalse(policy.killOnClose)
        assertTrue(policy.detachOnTimeout)
        assertFalse("rooted close must detach/park", policy.shouldCloseKill(rooted = true))
        assertTrue("non-rooted close may reclaim", policy.shouldCloseKill(rooted = false))
        assertTrue("finish timeout must not kill victim", policy.shouldKeepVictimOnTimeout(rooted = true))
        assertTrue(policy.shouldKeepVictimOnTimeout(rooted = false))
    }

    @Test
    fun `explicit kill policy overrides rooted detach`() {
        val policy = Cve43499VictimKillPolicy(
            killOnTimeout = true,
            killOnClose = true,
            detachOnTimeout = false,
        )
        assertTrue(policy.shouldCloseKill(rooted = true))
        assertFalse(policy.shouldKeepVictimOnTimeout(rooted = true))
        assertFalse(policy.detachOnTimeout)
    }

    @Test
    fun `factory and runtime carry kill policy`() {
        val custom = Cve43499VictimKillPolicy(killOnTimeout = true)
        assertEquals(custom, Cve43499VictimLaunchSpecFactory.policyOf(custom))
        val runtime = Cve43499CommonVictimRuntime(
            helperPath = "/data/app/lib/arm64/libghostlock_victim_helper.so",
            rawSizeBytes = 1L shl 20,
            killPolicy = custom,
        )
        assertEquals(custom, runtime.killPolicy())
        runtime.dispose()
    }
}
