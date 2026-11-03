package com.ghostlock.app.backend.backends.cve43499.flow

import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogSink
import com.ghostlock.app.log.log
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * task-101/103：43499 日志 seam/emit 的单测。
 *
 * 覆盖：UI 只显示 Info+、logcat/FileSink 双 sink、tag、脱敏/截断/控制字符、空白丢弃、
 * 两个 sink 抛异常互不吞噬、Warn/Error 正式 API、`Flow43499Request.progress` 默认 NONE。
 */
class Cve43499RunLogTest {
    private class CollectingSink : LogSink {
        val lines = mutableListOf<Triple<LogLevel, String, String>>()

        override fun write(level: LogLevel, tag: String, message: String, line: String) {
            lines += Triple(level, tag, message)
        }
    }

    @Test
    fun `emit forwards same line to progress and log with tag`() {
        val ui = mutableListOf<String>()
        val sink = CollectingSink()
        log.install(sink)
        try {
            Cve43499RunLog.emit(FlowProgress { ui += it }, LogLevel.Info, "w3 gate ok")
        } finally {
            log.install()
        }
        assertEquals(listOf("[43499] w3 gate ok"), ui)
        assertEquals(1, sink.lines.size)
        assertEquals(LogLevel.Info, sink.lines[0].first)
        assertEquals(Cve43499RunLog.TAG, sink.lines[0].second)
        assertEquals("w3 gate ok", sink.lines[0].third)
    }

    @Test
    fun `debug is log only and info is ui plus log`() {
        Cve43499RunLog.resetCounters()
        val ui = mutableListOf<String>()
        val sink = CollectingSink()
        log.install(sink)
        log.configure(minLevel = LogLevel.Debug)
        try {
            Cve43499RunLog.debug(FlowProgress { ui += it }, "w1 placement rounds=2")
            Cve43499RunLog.info(FlowProgress { ui += it }, "w1 verify ok")
        } finally {
            log.configure()
            log.install()
        }
        assertEquals(listOf("[43499] w1 verify ok"), ui)
        assertEquals(2, sink.lines.size)
        assertEquals(LogLevel.Debug, sink.lines[0].first)
        assertEquals("w1 placement rounds=2", sink.lines[0].third)
        assertEquals(LogLevel.Info, sink.lines[1].first)
    }

    @Test
    fun `sanitize replaces control and non ascii and truncates to 512 bytes`() {
        Cve43499RunLog.resetCounters()
        val ui = mutableListOf<String>()
        val sink = CollectingSink()
        log.install(sink)
        log.configure(minLevel = LogLevel.Debug)
        try {
            Cve43499RunLog.info(FlowProgress { ui += it }, "a\nb\tc✓")
            Cve43499RunLog.info(FlowProgress { ui += it }, "x".repeat(600))
            Cve43499RunLog.debug(FlowProgress { ui += it }, "detail only")
            Cve43499RunLog.warn(FlowProgress { ui += it }, "w1 verify unsupported", "verify-unsupported")
            Cve43499RunLog.emit(FlowProgress { ui += it }, LogLevel.Info, "   ")
        } finally {
            log.configure()
            log.install()
        }
        assertEquals(3, ui.size)
        assertTrue(ui[0].contains("a b c?"))
        assertTrue(ui[1].toByteArray(Charsets.US_ASCII).size <= Cve43499RunLog.MAX_LINE_BYTES)
        assertTrue(ui[2].contains("w1 verify unsupported: verify-unsupported"))
        assertTrue(Cve43499RunLog.sanitizedLines() >= 1)
        assertTrue(Cve43499RunLog.truncatedLines() >= 1)
        assertTrue(Cve43499RunLog.droppedLines() >= 1)
        assertTrue(sink.lines.any { it.first == LogLevel.Warn && it.third.contains("verify-unsupported") })
    }

    @Test
    fun `throwing progress does not swallow log and throwing sink does not escape`() {
        val ui = mutableListOf<String>()
        val sink = CollectingSink()
        log.install(sink)
        try {
            Cve43499RunLog.info(
                FlowProgress { error("progress boom") },
                "w1 placement ok",
            )
        } finally {
            log.install()
        }
        assertEquals(1, sink.lines.size)
        assertEquals("w1 placement ok", sink.lines[0].third)

        val throwingSink = object : LogSink {
            override fun write(level: LogLevel, tag: String, message: String, line: String) {
                error("sink boom")
            }
        }
        log.install(throwingSink)
        try {
            Cve43499RunLog.info(FlowProgress { ui += it }, "w1 land ok")
        } finally {
            log.install()
        }
        assertEquals(listOf("[43499] w1 land ok"), ui)
    }

    @Test
    fun `warn and error use reason suffix on ui and log`() {
        val ui = mutableListOf<String>()
        val sink = CollectingSink()
        log.install(sink)
        try {
            Cve43499RunLog.warn(FlowProgress { ui += it }, "w1 verify unverified", "verify-unverified")
            Cve43499RunLog.error(FlowProgress { ui += it }, "fail-stop", "stage=write.w2 reason=retained")
        } finally {
            log.install()
        }
        assertEquals(2, ui.size)
        assertEquals("[43499] w1 verify unverified: verify-unverified", ui[0])
        assertEquals("[43499] fail-stop: stage=write.w2 reason=retained", ui[1])
        assertEquals(2, sink.lines.size)
        assertEquals(LogLevel.Warn, sink.lines[0].first)
        assertEquals("w1 verify unverified: verify-unverified", sink.lines[0].third)
        assertEquals(LogLevel.Error, sink.lines[1].first)
    }

    @Test
    fun `request progress is append only default none`() {
        val request = Flow43499Request(
            session = Flow43499Request.Session(
                init = Flow43499Request.Init(route = Flow43499Route.SelectStack()),
            ),
        )
        assertSame(FlowProgress.NONE, request.progress)
        val custom = FlowProgress { }
        val withProgress = request.copy(progress = custom)
        assertSame(custom, withProgress.progress)
        assertFalse(withProgress.progress === FlowProgress.NONE)
    }
}
