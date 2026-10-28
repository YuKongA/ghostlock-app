package com.ghostlock.app.backend.cve43499

import com.ghostlock.app.backend.cve43499.finish.FinishAcceptance
import com.ghostlock.app.backend.cve43499.finish.FinishAcceptanceInput
import com.ghostlock.app.backend.cve43499.finish.FinishVerdict
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499Lifecycle
import com.ghostlock.app.backend.cve43499.victim.ProcessVictim
import com.ghostlock.app.backend.cve43499.victim.ProcessVictimSession
import com.ghostlock.app.backend.cve43499.victim.VictimFinishResult
import com.ghostlock.app.backend.cve43499.victim.VictimProcess
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-84 Kotlin 卫生项单测（先写不运行）。
 *
 * 覆盖：startVictim 幂等（不双重 spawn）；identity 只读/幂等（不发 probe 协议命令）；
 * ProcessVictim.close/uid 互斥（@Synchronized）；acceptance expectedUid 可配置。
 */
class KotlinHygieneTest {

    @Test
    fun `start victim is idempotent and never double spawns`() {
        var created = 0
        val lifecycle = Cve43499Lifecycle(
            victimFactory = {
                created++
                ProcessVictimSession(processFactory = { FakeVictimProcess() })
            },
        )
        assertNull(lifecycle.startVictim())
        assertNull(lifecycle.startVictim())
        assertEquals(1, created)
    }

    @Test
    fun `victim identity is read only and idempotent`() {
        val fake = FakeVictimProcess()
        val session = ProcessVictimSession(
            processFactory = { fake },
            statReader = {
                "1 (victim) S " + (List(18) { "0" } + listOf("777")).joinToString(" ")
            },
            statusUidReader = { 0L },
        )
        assertTrue(session.start() is com.ghostlock.app.backend.cve43499.victim.VictimUidResult.Ok)
        val first = session.identity()
        val second = session.identity()
        assertEquals(first, second)
        assertEquals(777L, first?.startTimeTicks)
        assertEquals(0L, first?.uid)
        assertEquals("identity must not issue uid probe command", 0, fake.uidCalls)
    }

    @Test
    fun `process victim close and uid are synchronized`() {
        val close = ProcessVictim::class.java.getDeclaredMethod("close")
        val uid = ProcessVictim::class.java.getDeclaredMethod("uid")
        assertTrue(
            "@Synchronized close",
            java.lang.reflect.Modifier.isSynchronized(close.modifiers),
        )
        assertTrue(
            "@Synchronized uid",
            java.lang.reflect.Modifier.isSynchronized(uid.modifiers),
        )
    }

    @Test
    fun `acceptance expected uid is configurable`() {
        fun reasons(expected: Long, observed: Long): List<String> =
            (FinishAcceptance.evaluate(
                FinishAcceptanceInput(
                    runId = "run",
                    expectedUid = expected,
                    observedUid = observed,
                    handoff = null,
                    resourcesReleased = false,
                    execution = null,
                    binding = null,
                ),
            ) as FinishVerdict.Refused).reasons
        assertTrue(reasons(0L, 0L).none { it.startsWith("finish-uid") })
        assertTrue(reasons(2_000L, 0L).contains("finish-uid-not-root:0"))
        assertTrue(reasons(0L, 2_000L).contains("finish-uid-not-root:2000"))
    }

    private class FakeVictimProcess : VictimProcess {
        var uidCalls: Int = 0
        override fun uid(): Int? {
            uidCalls++
            return 0
        }

        override fun pid(): Int = 1_234
        override fun finish(commandLine: String): VictimFinishResult =
            VictimFinishResult.Failed("victim-finish:unsupported")
        override fun park(): Boolean = true
        override fun bye(): Boolean = true
        override fun close() = Unit
    }
}
