package com.ghostlock.app.backend.cve43499.lifecycle

import com.ghostlock.app.backend.cve43499.victim.VictimFinishResult
import com.ghostlock.app.core.victim.VictimIdentity
import com.ghostlock.app.backend.cve43499.victim.VictimProbeKind
import com.ghostlock.app.backend.cve43499.victim.VictimSession
import com.ghostlock.app.backend.cve43499.victim.VictimUidResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 生命周期层 rooted victim 终结策略单测（R3f/task-45）：rooted 只 detach 不 kill；
 * 非 rooted 正常 close；victimFinish/rooted 委托。
 */
class Cve43499LifecycleRootedTest {
    @Test
    fun `rooted victim is detached not closed`() {
        val session = FakeVictimSession(rooted = true)
        val lifecycle = Cve43499Lifecycle(victimFactory = { session })
        assertNull(lifecycle.startVictim())
        assertTrue(lifecycle.victimRooted())
        val receipt = lifecycle.closeAll()
        assertEquals(1, session.detachCalls)
        assertEquals(0, session.closeCalls)
        assertTrue(receipt.parkedRootedChild)
        assertTrue(receipt.termination.contains("rooted-child-parked"))
        assertTrue(receipt.allReleased)
        assertFalse(receipt.retained)
    }

    @Test
    fun `non rooted victim closes normally`() {
        val session = FakeVictimSession(rooted = false)
        val lifecycle = Cve43499Lifecycle(victimFactory = { session })
        assertNull(lifecycle.startVictim())
        assertFalse(lifecycle.victimRooted())
        val receipt = lifecycle.closeAll()
        assertEquals(0, session.detachCalls)
        assertEquals(1, session.closeCalls)
        assertFalse(receipt.parkedRootedChild)
        assertTrue(receipt.termination.contains("victim=closed"))
    }

    @Test
    fun `victim finish delegates to session`() {
        val session = FakeVictimSession(
            rooted = true,
            finishResult = VictimFinishResult.Ok(7, "sha", false),
        )
        val lifecycle = Cve43499Lifecycle(victimFactory = { session })
        lifecycle.startVictim()
        assertEquals(
            VictimFinishResult.Ok(7, "sha", false),
            lifecycle.victimFinish("/tmp/run.sh", 1_234L),
        )
        assertEquals(1, session.finishCalls)
        assertEquals("/tmp/run.sh", session.lastScriptPath)
        assertEquals(1_234L, session.lastTimeoutMs)
    }

    @Test
    fun `victim finish before start fails closed`() {
        val session = FakeVictimSession(rooted = true)
        val lifecycle = Cve43499Lifecycle(victimFactory = { session })
        val result = lifecycle.victimFinish("/tmp/run.sh", 1_000L)
        assertEquals(VictimFinishResult.Failed("victim-finish:not-started"), result)
        assertEquals(0, session.finishCalls)
    }

    /** 记录调用次序的 victim 会话 fake。 */
    private class FakeVictimSession(
        override val rooted: Boolean,
        private val finishResult: VictimFinishResult =
            VictimFinishResult.Failed("victim-finish:unsupported"),
    ) : VictimSession {
        var detachCalls: Int = 0
        var closeCalls: Int = 0
        var finishCalls: Int = 0
        var lastScriptPath: String? = null
        var lastTimeoutMs: Long? = null

        override val kind: VictimProbeKind get() = VictimProbeKind.FIXTURE

        override fun start(): VictimUidResult = VictimUidResult.Ok(0L)

        override fun uid(): VictimUidResult =
            VictimUidResult.Ok(if (rooted) 0L else 2_000L)

        override fun identity(): VictimIdentity = VictimIdentity(
            pid = 1_234,
            startTimeTicks = 777L,
            uid = if (rooted) 0L else 2_000L,
        )

        override fun finish(scriptPath: String, timeoutMs: Long): VictimFinishResult {
            finishCalls++
            lastScriptPath = scriptPath
            lastTimeoutMs = timeoutMs
            return finishResult
        }

        override fun detachRooted(): Boolean {
            detachCalls++
            return true
        }

        override fun close() {
            closeCalls++
        }
    }
}
