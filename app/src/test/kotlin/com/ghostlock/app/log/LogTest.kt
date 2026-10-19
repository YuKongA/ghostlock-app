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
class LogTest {
    private class Recorder : LogSink {
        val lines = mutableListOf<String>()

        override fun write(level: LogLevel, tag: String, message: String, line: String) {
            lines += level.token + "|" + tag + "|" + line
        }
    }

    private val recorder = Recorder()

    @After
    fun restore() {
        log.install()
        log.configure(LogLevel.Info)
        log.clear()
    }

    @Test
    fun aLineBelowTheMinimumIsNotWrittenAnywhere() {
        log.install(recorder)
        log.configure(LogLevel.Warn)
        assertFalse("debug 低于 warn ⇒ 不写", log.invoke(LogLevel.Debug, "tag", "debug line"))
        assertTrue(recorder.lines.isEmpty())
        assertTrue(log.snapshot().isEmpty())
        assertTrue("warn 达到门槛 ⇒ 写", log.invoke(LogLevel.Warn, "tag", "warn line"))
        assertEquals(1, recorder.lines.size)
        assertEquals(1, log.snapshot().size)
    }

    @Test
    fun theRingKeepsTheLastLinesOnly() {
        log.install(recorder)
        log.configure(LogLevel.Info, ringCapacity = 3)
        for (index in 1..5) {
            log.invoke(LogLevel.Info, "tag", "line " + index)
        }
        val ring = log.snapshot()
        assertEquals(3, ring.size)
        assertTrue("最旧的先被挤出去", ring.first().endsWith("line 3"))
        assertTrue(ring.last().endsWith("line 5"))
    }

    @Test
    fun theLevelLetterIsInTheLine() {
        log.install(recorder)
        log.configure(LogLevel.Verbose)
        log.error("boom")
        // 去处收到的是 level.token|tag|行；行本身是窄屏形状 <级别 时间> 正文（与 native 侧面括号同理）。
        val line = recorder.lines.first()
        assertTrue(line.startsWith("error|Ghostlock|<!"))
        assertTrue(line.endsWith("> boom"))
    }

    @Test
    fun anErrorCarriesItsStack() {
        log.install(recorder)
        log.configure(LogLevel.Info)
        log.error( "failed", IllegalStateException("because"))
        assertEquals("一条错误 + 最多 16 帧", 17, recorder.lines.size)
        assertTrue(recorder.lines.first().contains("because"))
        assertTrue(recorder.lines[1].contains("at "))
    }
}
