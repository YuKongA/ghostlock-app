package com.ghostlock.app.log

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 日志门面自己的规则：级别过滤、环的容量、去处被调用，以及错误会带上堆栈。
 *
 * 去处是可替换的，所以这些规则在 JVM 单测里跑（真机上的 logcat 去处由设备门禁验）。
 */
class GhostLogTest {
    private class Recorder : LogSink {
        val lines = mutableListOf<String>()

        override fun write(level: LogLevel, tag: String, message: String, line: String) {
            lines += level.token + "|" + tag + "|" + line
        }
    }

    private val recorder = Recorder()

    @After
    fun restore() {
        GhostLog.install()
        GhostLog.configure(LogLevel.Info)
        GhostLog.clear()
    }

    @Test
    fun aLineBelowTheMinimumIsNotWrittenAnywhere() {
        GhostLog.install(recorder)
        GhostLog.configure(LogLevel.Warn)
        assertFalse("debug 低于 warn ⇒ 不写", GhostLog.d("tag", "debug line"))
        assertTrue(recorder.lines.isEmpty())
        assertTrue(GhostLog.snapshot().isEmpty())
        assertTrue("warn 达到门槛 ⇒ 写", GhostLog.w("tag", "warn line"))
        assertEquals(1, recorder.lines.size)
        assertEquals(1, GhostLog.snapshot().size)
    }

    @Test
    fun theRingKeepsTheLastLinesOnly() {
        GhostLog.install(recorder)
        GhostLog.configure(LogLevel.Info, ringCapacity = 3)
        for (index in 1..5) {
            GhostLog.i("tag", "line " + index)
        }
        val ring = GhostLog.snapshot()
        assertEquals(3, ring.size)
        assertTrue("最旧的先被挤出去", ring.first().endsWith("line 3"))
        assertTrue(ring.last().endsWith("line 5"))
    }

    @Test
    fun theLevelLetterIsInTheLine() {
        GhostLog.install(recorder)
        GhostLog.configure(LogLevel.Verbose)
        GhostLog.e("tag", "boom")
        // 去处收到的是 level.token|tag|行；行本身以级别字母开头（与 native 侧同一形状）。
        assertTrue(recorder.lines.first().startsWith("error|tag|E/tag: boom"))
    }

    @Test
    fun anErrorCarriesItsStack() {
        GhostLog.install(recorder)
        GhostLog.configure(LogLevel.Info)
        GhostLog.e("tag", "failed", IllegalStateException("because"))
        assertEquals("一条错误 + 最多 16 帧", 17, recorder.lines.size)
        assertTrue(recorder.lines.first().contains("because"))
        assertTrue(recorder.lines[1].contains("at "))
    }
}
