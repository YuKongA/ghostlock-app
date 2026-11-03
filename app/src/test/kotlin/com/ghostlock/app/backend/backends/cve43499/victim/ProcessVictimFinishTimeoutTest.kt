package com.ghostlock.app.backend.backends.cve43499.victim

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A1 delta（task-72/I2）：`ProcessVictimSession.finish` 超时只 detach/park，不 close/kill victim。
 *
 * 仅写不跑（用户令「先编码不编译」）：fake victim finish 阻塞超过 timeout；
 * 断言返回 Failed(retained=true)、park 已请求、close 从未调用（victim 存活）。
 */
class ProcessVictimFinishTimeoutTest {

    @Test
    fun `finish timeout parks victim instead of close or kill`() {
        val fake = BlockingVictimProcess()
        val session = ProcessVictimSession(
            processFactory = { fake },
            probeTimeoutMs = 50L,
        )
        assertTrue(session.start() is VictimUidResult.Ok)
        val result = session.finish("/data/local/tmp/glk-finish.sh", timeoutMs = 10L)
        assertTrue(result is VictimFinishResult.Failed)
        val failed = result as VictimFinishResult.Failed
        assertTrue("timeout failure must mark victim retained", failed.retained)
        assertTrue("timeout failure token", failed.reason.startsWith("victim-finish:timeout"))
        assertTrue("victim must be parked (not killed)", fake.parkCalled)
        assertFalse("victim must not be closed/killed on finish timeout", fake.closeCalled)
    }

    /** 阻塞 finish 的 fake victim（不真起进程）。 */
    private class BlockingVictimProcess : VictimProcess {
        var parkCalled: Boolean = false
        var closeCalled: Boolean = false

        override fun uid(): Int? = 2000

        override fun finish(commandLine: String): VictimFinishResult {
            /* 阻塞超过 session 的 timeout（不响应）⇒ 走 timeout detach 分支。 */
            try {
                Thread.sleep(10_000L)
            } catch (interrupted: InterruptedException) {
                Thread.currentThread().interrupt()
            }
            return VictimFinishResult.Failed("victim-finish:unsupported")
        }

        override fun park(): Boolean {
            parkCalled = true
            return true
        }

        override fun bye(): Boolean = true

        override fun close() {
            closeCalled = true
        }
    }
}
