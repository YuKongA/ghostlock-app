package com.ghostlock.app.domain.run

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Locks the complete GLLOG1 literal grammar without Android storage or a reverse parser. */
class RunLogDocumentFormatterTest {
    private val formatter = RunLogDocumentFormatter()

    /** A manual snapshot before the first accepted run uses the sole placeholder identity. */
    @Test
    fun noAcceptedRunUsesExactPlaceholderMeta() {
        val text = formatter.format(
            RunLogDocumentSnapshot(
                identity = null,
                records = emptyList(),
                phase = RunSessionPhase.Idle,
                report = null,
                config = RunLogFormatConfig(showTimestamp = false),
            ),
        ).toString(Charsets.UTF_8)

        assertEquals(
            "GLLOG1\n" +
                "meta\trun_id=-\tgeneration=0\tbackend=-\n" +
                "session\tphase=Idle\n" +
                "report\tpresent=false\n" +
                "end\tcomplete=true\n",
            text,
        )
    }

    /** Streaming primitives and complete snapshot formatting are byte-identical. */
    @Test
    fun streamingAndSnapshotBytesAreIdentical() {
        val identity = RunLogDocumentIdentity("run-1", 7L, "backend\\key")
        val config = RunLogFormatConfig(showTimestamp = true)
        val records = listOf(
            RunLogRecord(
                origin = RunLogOrigin.Kotlin,
                level = RunLogLevel.Warn,
                message = "a\\b\r\nc\td-世界",
                elapsedMs = 42L,
            ),
        )
        val report = completeReport()
        val streamed = buildList {
            addAll(formatter.formatHeader(identity, records, config))
            addAll(formatter.formatTrailer(RunSessionPhase.Done, report))
        }.fold(ByteArray(0)) { bytes, row -> bytes + row }
        val snapshot = formatter.format(
            RunLogDocumentSnapshot(
                identity = identity,
                records = records,
                phase = RunSessionPhase.Done,
                report = report,
                config = config,
            ),
        )

        assertArrayEquals(snapshot, streamed)
        val text = snapshot.toString(Charsets.UTF_8)
        assertTrue(text.endsWith("end\tcomplete=true\n"))
        assertTrue(
            text.contains(
                "entry\torigin=Kotlin\tlevel=Warn\telapsed_ms=42\tmessage=a\\\\b\\r\\nc\\td-世界\n",
            ),
        )
        assertFalse(text.contains("\r"))
        assertEquals(text.lines().dropLast(1).size, text.count { character -> character == '\n' })
    }

    /** Every typed report field appears once in its frozen literal order. */
    @Test
    fun reportRowsUseExactOrderAndProductAuthority() {
        val text = formatter.format(
            RunLogDocumentSnapshot(
                identity = RunLogDocumentIdentity("abc", 1L, "debug-probe"),
                records = emptyList(),
                phase = RunSessionPhase.HandoffRequested,
                report = completeReport(),
                config = RunLogFormatConfig(showTimestamp = false),
            ),
        ).toString(Charsets.UTF_8)
        val expectedRows = listOf(
            "report\tpresent=true",
            "report\trun_capability=PRODUCT_ROOT",
            "report\tdiagnostic_outcome=NOT_APPLICABLE",
            "stage\tname=backend_primitive\tstate=SUCCESS\treason=NONE\texit_kind=NONE\texit_value=0",
            "stage\tname=root_script_dispatch\tstate=SUCCESS\treason=NONE\texit_kind=NONE\texit_value=0",
            "payload\tindex=0\titem_id=payload\\t1\tstate=SUCCESS\treason=NONE\texit_kind=PROCESS_EXIT\texit_value=0",
            "stage\tname=payload_overall\tstate=SUCCESS\treason=NONE\texit_kind=NONE\texit_value=0",
            "finish\tindex=0\titem_id=finish\\n1\tstate=SUCCESS\treason=NONE\texit_kind=PROCESS_EXIT\texit_value=0",
            "stage\tname=finish_overall\tstate=SUCCESS\treason=NONE\texit_kind=NONE\texit_value=0",
            "stage\tname=backend_cleanup\tstate=SUCCESS\treason=NONE\texit_kind=NONE\texit_value=0",
            "report\tterminal=SUCCESS",
            "report\tproduct_success=true",
        )
        var prior = -1
        expectedRows.forEach { row ->
            val position = text.indexOf(row)
            assertTrue("missing or reordered row: $row", position > prior)
            prior = position
        }
    }

    /** @return Complete product report with one Payload and one Finish item. */
    private fun completeReport(): RunReport = RunReport(
        runCapability = RunCapability.PRODUCT_ROOT,
        diagnosticOutcome = DiagnosticOutcome.NOT_APPLICABLE,
        backendPrimitive = StageResult.SUCCESS,
        rootScriptDispatch = StageResult.SUCCESS,
        payloadItems = listOf(
            PayloadItemResult(
                itemId = "payload\t1",
                state = ItemState.SUCCESS,
                reason = ResultReason.NONE,
                exitKind = ExitKind.PROCESS_EXIT,
                exitValue = 0,
            ),
        ),
        payloadOverall = StageResult.SUCCESS,
        finishItems = listOf(
            FinishItemResult(
                itemId = "finish\n1",
                state = ItemState.SUCCESS,
                reason = ResultReason.NONE,
                exitKind = ExitKind.PROCESS_EXIT,
                exitValue = 0,
            ),
        ),
        finishOverall = StageResult.SUCCESS,
        backendCleanup = StageResult.SUCCESS,
        terminal = RunTerminal.SUCCESS,
    )
}
