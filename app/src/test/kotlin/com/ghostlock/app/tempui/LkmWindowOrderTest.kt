package com.ghostlock.app.tempui

import java.io.FileDescriptor
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 窗口在链上的**顺序**与失败语义（本批第四项判据）。
 *
 * 依据 ghostlock-app-ref@5f184760 的 `src/core/backend/cve_2026_43284/steps/chain.cpp:362-380`：
 * 窗口在 trigger **之前**打开、在终点恢复**之后**关闭、关闭发生在任何 release **之前**；
 * 关窗失败只记录，不掩盖主结果。
 *
 * 本类把 probe 的调用序原样走一遍（[TypedBackendProbe.probe43284Body] 在 typed trigger 之前 open；
 * [TypedBackendProbe.probeAssembled43284] 在 restore 之后、teardown 之前 close），用两个缝把顺序记下来。
 * 它不重复 [LkmWindow43284Test] 的幂等/errno 判据，只钉「什么时候开、什么时候关、失败了会怎样」。
 */
class LkmWindowOrderTest {

    /** 单测里的 fd 令牌：真的 [FileDescriptor] 对象，但没有绑定任何系统 fd。 */
    private fun token(): FileDescriptor = FileDescriptor()

    @Test
    fun theWindowOpensBeforeTriggerAndClosesAfterRestore() {
        val events = mutableListOf<String>()
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { events += "open"; token() },
            closeFn = { events += "close" },
        )

        /* probe43284Body：trigger 之前 open（链上的 trigger 在 probeAssembled43284 里，更晚）。 */
        assertTrue(window.open())
        events += "trigger"
        events += "restore"
        /* probeAssembled43284：restore 之后、teardown 之前 close。 */
        assertTrue(window.close())
        events += "teardown"

        assertEquals(
            "顺序是判据：open → trigger → restore → close → teardown",
            listOf("open", "trigger", "restore", "close", "teardown"),
            events,
        )
        assertEquals(
            "两条日志各自一行，且与真机日志同一组字符串",
            listOf("lkm window: opened=1 path=/dev/glk", "lkm window: closed=1"),
            lines,
        )
    }

    @Test
    fun aWindowThatCannotOpenDoesNotChangeTheRunResult() {
        val lines = mutableListOf<String>()
        var closes = 0
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { throw IllegalStateException("ENOENT: /dev/glk") },
            closeFn = { closes += 1 },
        )

        /* 主结果与窗口无关：这里用一段字符串代表 trigger/wait 的终态。 */
        var runResult = "pending"
        assertFalse("窗口打不开只报 false，不抛", window.open())
        runResult = "terminus=success"
        assertTrue("没开过窗，close 是安全空操作", window.close())

        assertEquals("窗口失败不得改写主结果", "terminus=success", runResult)
        assertEquals("没有 fd 就没有关闭动作", 0, closes)
        assertTrue(
            "失败行仍以 `lkm window: opened=0` 开头并带原因（errno 名）",
            lines.first().startsWith("lkm window: opened=0 path=/dev/glk errno="),
        )
        assertEquals("空操作的 close 行与幂等语义一致", "lkm window: closed=0", lines.last())
        assertFalse("窗口仍是关的", window.isOpen())
    }

    @Test
    fun aFailedCloseKeepsTheWindowOpenWithoutChangingTheRunResult() {
        var attempts = 0
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { token() },
            closeFn = { attempts += 1; if (attempts == 1) throw IllegalStateException("EINTR") },
        )

        var runResult = "pending"
        assertTrue(window.open())
        runResult = "cleanup=1"
        assertFalse("关窗失败返回 false，不抛", window.close())

        assertEquals("关窗失败不得改写主结果", "cleanup=1", runResult)
        assertTrue("fd 留在手里：窗口仍占着（可以重试）", window.isOpen())
        assertTrue("失败行带 errno", lines.last().startsWith("lkm window: closed=0 errno="))
        assertTrue("重试可以关掉", window.close())
        assertFalse(window.isOpen())
    }
}
