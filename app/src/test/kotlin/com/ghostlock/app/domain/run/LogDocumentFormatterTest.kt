package com.ghostlock.app.domain.run

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Locks the exact row-only GLLOG2 grammar without Android storage or a reverse parser. */
class LogDocumentFormatterTest {
    private val formatter = LogDocumentFormatter()

    @Test
    fun idleDocumentHasPlaceholderIdentityAndNoTypedReportTrailer() {
        val rows = formatter.formatHeader(null) + formatter.formatTrailer(RunSessionPhase.Idle)
        assertEquals(
            "GLLOG2\nmeta\trun_id=-\tgeneration=0\tbackend=-\n" +
                "session\tphase=Idle\nend\tterminal_received=true\n",
            rows.joinToString("") { it.toString(Charsets.UTF_8) },
        )
    }

    @Test
    fun committedRowHasStableIdAndEscapedCells() {
        val record = LogRecord(
            origin = LogOrigin.Kotlin,
            level = LogLevel.Warn,
            message = "a\\b\r\nc\td-世界",
            elapsedMs = 42L,
            commitId = 7L,
        )
        val bytes = formatter.formatRecord(record, LogFormatConfig(showTimestamp = true))
        assertEquals(
            "entry\tcommit_id=7\torigin=Kotlin\tlevel=Warn\telapsed_ms=42\tmessage=a\\\\b\\r\\nc\\td-世界\n",
            bytes.toString(Charsets.UTF_8),
        )
        assertFalse(bytes.contains('\r'.code.toByte()))
        assertEquals(1, bytes.count { it == '\n'.code.toByte() })
    }

    @Test
    fun onePhysicalGrammarContainsNoReportOrStageRows() {
        val rows = formatter.formatHeader(LogDocumentIdentity("r1", 5L, "diagnose")) +
            formatter.formatRecord(
                LogRecord(LogOrigin.Kotlin, LogLevel.Info, "Product success: NO", commitId = 1L),
                LogFormatConfig(false),
            ) + formatter.formatTrailer(RunSessionPhase.Done)
        val text = rows.joinToString("") { it.toString(Charsets.UTF_8) }
        assertTrue(text.startsWith("GLLOG2\nmeta\trun_id=r1\tgeneration=5\tbackend=diagnose\n"))
        assertTrue(text.endsWith("session\tphase=Done\nend\tterminal_received=true\n"))
        assertFalse(text.contains("report\t"))
        assertFalse(text.contains("stage\t"))
    }
}
