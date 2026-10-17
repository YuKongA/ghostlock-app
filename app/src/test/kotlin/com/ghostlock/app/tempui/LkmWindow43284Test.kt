package com.ghostlock.app.tempui

import java.io.FileDescriptor
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * LKM 驻留窗口的四条判据（照 ghostlock-app-ref@5f184760:steps/chain.cpp:362-380）。
 *
 * 1. open 失败只返回 false、只写一行日志，**从不抛**；失败之后还能再试；
 * 2. open 幂等：重复 open 不重复开 fd；
 * 3. close 幂等：没开就 close 是安全的空操作，重复 close 不会重复关 fd；
 * 4. open → close → open 允许（窗口是会话绑定的）。
 *
 * 日志行是本类的对外事实（`lkm window: opened=<0|1> path=…` / `closed=<0|1>`），
 * 所以失败路径也逐行断言 —— 真机日志与单测看的是同一组字符串。
 */
class LkmWindow43284Test {

    /** 单测里的 fd 令牌：真的 [FileDescriptor] 对象，但没有绑定任何系统 fd。 */
    private fun token(): FileDescriptor = FileDescriptor()

    @Test
    fun openingTwiceOpensOneFdAndLogsTheWindow() {
        var opens = 0
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { opens += 1; token() },
            closeFn = { },
        )

        assertTrue(window.open())
        assertTrue("已开着再 open 也报成功", window.open())
        assertEquals("重复 open 不得再开第二个 fd（模块 single-open）", 1, opens)
        assertTrue(window.isOpen())
        assertEquals(
            "每次 open 一行，路径是设备节点原文",
            listOf(
                "lkm window: opened=1 path=/dev/glk",
                "lkm window: opened=1 path=/dev/glk",
            ),
            lines,
        )
    }

    @Test
    fun aFailedOpenReturnsFalseWithoutThrowingAndCanBeRetried() {
        var fail = true
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { if (fail) throw IllegalStateException("no device") else token() },
            closeFn = { },
        )

        assertFalse("open 失败只返回 false，不抛", window.open())
        assertFalse(window.isOpen())
        assertTrue(
            "失败路径也要打日志行（带 errno 字段）",
            lines.single().startsWith("lkm window: opened=0 path=/dev/glk errno="),
        )

        fail = false
        assertTrue("上次失败不影响下一次 open", window.open())
        assertTrue(window.isOpen())
    }

    @Test
    fun errnoNamesAreSpelledOutForTheLog() {
        assertEquals("errno 13 必须写名字（JVM 上退化到常用表）", "EACCES", LkmWindow43284.errnoName(13))
        assertEquals("节点不存在是最常见的失败", "ENOENT", LkmWindow43284.errnoName(2))
        assertEquals("表外 errno 至少保留数字", "E9999", LkmWindow43284.errnoName(9999))
    }

    @Test
    fun closingBeforeOpeningIsAnIdempotentNoOp() {
        var closes = 0
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { token() },
            closeFn = { closes += 1 },
        )

        assertTrue("没开就 close 视为成功", window.close())
        assertTrue(window.close())
        assertEquals("没有 fd 就不得调用关闭", 0, closes)
        assertEquals("空操作也各打一行 closed=0", listOf("lkm window: closed=0", "lkm window: closed=0"), lines)
    }

    @Test
    fun closingTwiceClosesOneFdAndLogsOnce() {
        val first = token()
        var closes = 0
        val closedFds = mutableListOf<FileDescriptor>()
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { first },
            closeFn = { fd -> closes += 1; closedFds += fd },
        )

        assertTrue(window.open())
        assertTrue(window.close())
        assertTrue("第二次 close 是安全空操作", window.close())
        assertEquals("close 只关一次", 1, closes)
        assertEquals("关的是 open 拿到的那一个 fd", listOf(first), closedFds)
        assertFalse(window.isOpen())
        assertEquals(
            listOf("lkm window: opened=1 path=/dev/glk", "lkm window: closed=1", "lkm window: closed=0"),
            lines,
        )
    }

    @Test
    fun openCloseOpenIsAllowed() {
        var opens = 0
        val closedFds = mutableListOf<FileDescriptor>()
        val window = LkmWindow43284(
            openFn = { opens += 1; token() },
            closeFn = { fd -> closedFds += fd },
        )

        assertTrue(window.open())
        assertTrue(window.close())
        assertTrue("窗口是会话绑定的，关掉之后可以再开", window.open())
        assertEquals(2, opens)
        assertEquals("只有第一次 open 的 fd 被关掉（第二次还开着）", 1, closedFds.size)
        assertTrue(window.isOpen())
    }

    @Test
    fun aFailedCloseKeepsTheFdForRetryAndLogsTheErrno() {
        var attempts = 0
        val lines = mutableListOf<String>()
        val window = LkmWindow43284(
            emit = { line -> lines += line },
            openFn = { token() },
            closeFn = { attempts += 1; if (attempts == 1) throw IllegalStateException("interrupted") },
        )

        assertTrue(window.open())
        assertFalse("close 失败返回 false", window.close())
        assertTrue("fd 留在手里，窗口仍算开着", window.isOpen())
        assertTrue("失败行带 errno 字段", lines.last().startsWith("lkm window: closed=0 errno="))
        assertTrue("重试可以关掉", window.close())
        assertFalse(window.isOpen())
        assertEquals("一共两次尝试、一次成功", 2, attempts)
    }

    @Test
    fun theOpenPathIsAParameter() {
        var seen = ""
        val window = LkmWindow43284(
            openFn = { path -> seen = path; token() },
            closeFn = { },
        )

        assertTrue(window.open("/dev/glk-test"))
        assertEquals("open(path) 必须把路径透给打开缝", "/dev/glk-test", seen)
    }
}
