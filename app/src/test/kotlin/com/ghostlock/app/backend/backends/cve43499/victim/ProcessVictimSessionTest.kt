package com.ghostlock.app.backend.backends.cve43499.victim

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * ProcessVictimSession 单测（R3f/task-45）：rooted 置位、finish 成功/失败/超时、
 * rooted detach 不 kill、非 rooted close 正常回收。
 *
 * @note 用 [VictimProcess] fake 驱动协议；不启动真实 shell、不触 Android。
 */
class ProcessVictimSessionTest {
    @Test
    fun `rooted flag set when uid probe reads zero`() {
        val fake = FakeVictimProcess(uidValue = 0)
        val session = ProcessVictimSession(processFactory = { fake })
        try {
            assertEquals(VictimUidResult.Ok(0L), session.start())
            assertFalse(session.rooted)
            assertEquals(VictimUidResult.Ok(0L), session.uid())
            assertTrue(session.rooted)
            assertTrue(fake.commands.contains(VictimProtocol.COMMAND_PROBE))
        } finally {
            session.close()
        }
    }

    @Test
    fun `finish delegates protocol line and returns exit plus digest`() {
        val fake = FakeVictimProcess(
            uidValue = 0,
            finishResult = VictimFinishResult.Ok(
                exitCode = 0,
                outputSha256 = "deadbeef",
                timedOut = false,
            ),
        )
        val session = ProcessVictimSession(processFactory = { fake })
        try {
            session.start()
            session.uid()
            val result = session.finish("/data/user/0/app/files/finish/run.sh", 5_000L)
            assertEquals(VictimFinishResult.Ok(0, "deadbeef", false), result)
            assertTrue(
                fake.commands.last().startsWith("finish 5 '/data/user/0/app/files/finish/run.sh'"),
            )
        } finally {
            session.close()
        }
    }

    @Test
    fun `finish failure passes named reason`() {
        val fake = FakeVictimProcess(
            uidValue = 0,
            finishResult = VictimFinishResult.Failed("victim-finish:epipe"),
        )
        val session = ProcessVictimSession(processFactory = { fake })
        try {
            session.start()
            val result = session.finish("/tmp/run.sh", 1_000L)
            assertEquals(VictimFinishResult.Failed("victim-finish:epipe"), result)
        } finally {
            session.close()
        }
    }

    @Test
    fun `finish rejects invalid script path without touching victim`() {
        val fake = FakeVictimProcess(uidValue = 0)
        val session = ProcessVictimSession(processFactory = { fake })
        try {
            session.start()
            val result = session.finish("relative/run.sh", 1_000L)
            assertTrue(result is VictimFinishResult.Failed)
            assertTrue(
                (result as VictimFinishResult.Failed).reason
                    .startsWith("victim-finish:script-path:"),
            )
            assertTrue(fake.commands.none { it.startsWith("finish ") })
        } finally {
            session.close()
        }
    }

    @Test
    fun `finish timeout fails closed and closes session`() {
        val fake = FakeVictimProcess(
            uidValue = 0,
            finishBlocker = CountDownLatch(1),
        )
        val session = ProcessVictimSession(processFactory = { fake })
        session.start()
        val startedAt = System.nanoTime()
        val result = session.finish("/tmp/hung.sh", 100L)
        val elapsedMs = (System.nanoTime() - startedAt) / 1_000_000L
        assertEquals(
            VictimFinishResult.Failed("victim-finish:timeout", retained = true),
            result,
        )
        assertTrue("有界等待应覆盖 timeout+排空余量，elapsed=${elapsedMs}ms", elapsedMs >= 100L)
        assertEquals(
            VictimUidResult.Failed("victim-probe:closed"),
            session.uid(),
        )
        assertFalse("timeout 只 detach/park，不 close victim", fake.closeCalled)
    }

    @Test
    fun `detach rooted parks without closing process`() {
        val fake = FakeVictimProcess(uidValue = 0)
        val session = ProcessVictimSession(processFactory = { fake })
        session.start()
        session.uid()
        assertTrue(session.rooted)
        assertTrue(session.detachRooted())
        assertTrue(fake.parkCalled)
        assertFalse("rooted child 不得被 kill/close", fake.closeCalled)
        assertFalse(fake.byeCalled)
        assertEquals(VictimUidResult.Failed("victim-probe:closed"), session.uid())
    }

    @Test
    fun `detach refused for non rooted session`() {
        val fake = FakeVictimProcess(uidValue = 2_000)
        val session = ProcessVictimSession(processFactory = { fake })
        try {
            session.start()
            session.uid()
            assertFalse(session.rooted)
            assertFalse(session.detachRooted())
            assertFalse(fake.parkCalled)
            assertFalse(fake.closeCalled)
        } finally {
            session.close()
        }
    }

    @Test
    fun `non rooted close sends bye and closes process`() {
        val fake = FakeVictimProcess(uidValue = 2_000)
        val session = ProcessVictimSession(processFactory = { fake })
        session.start()
        session.close()
        assertTrue(fake.byeCalled)
        assertTrue(fake.closeCalled)
    }

    /** 可脚本化的 [VictimProcess] fake（只覆盖本测试需要的协议命令）。 */
    private class FakeVictimProcess(
        private val uidValue: Int? = 0,
        private val finishResult: VictimFinishResult =
            VictimFinishResult.Ok(0, "sha", false),
        private val finishBlocker: CountDownLatch? = null,
    ) : VictimProcess {
        val commands: MutableList<String> = mutableListOf()
        @Volatile var closeCalled: Boolean = false
        @Volatile var parkCalled: Boolean = false
        @Volatile var byeCalled: Boolean = false

        override fun uid(): Int? {
            commands += VictimProtocol.COMMAND_PROBE
            return uidValue
        }

        override fun finish(commandLine: String): VictimFinishResult {
            commands += commandLine
            finishBlocker?.await(10L, TimeUnit.SECONDS)
            return finishResult
        }

        override fun park(): Boolean {
            parkCalled = true
            return true
        }

        override fun bye(): Boolean {
            byeCalled = true
            return true
        }

        override fun close() {
            closeCalled = true
        }
    }
}
