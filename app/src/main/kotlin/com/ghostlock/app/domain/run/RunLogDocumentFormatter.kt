package com.ghostlock.app.domain.run

import java.io.ByteArrayOutputStream

/** Immutable formatting options frozen at the accepted-run boundary. */
data class RunLogFormatConfig(
    val showTimestamp: Boolean,
)

/**
 * Immutable identity of the accepted run that owns a canonical document.
 *
 * Null identity is reserved for a manual snapshot before this process has accepted any run.
 */
data class RunLogDocumentIdentity(
    val runId: String,
    val generation: Long,
    val backendKey: String,
)

/**
 * Immutable complete view consumed by the sole GLLOG1 formatter.
 *
 * @property identity Accepted-run identity, or null before the first accepted run.
 * @property records Canonical records unaffected by presentation clear operations.
 * @property phase Session phase captured with [report].
 * @property report Complete typed terminal authority, or null before/without a report.
 * @property config Formatting configuration frozen with the owning run.
 */
data class RunLogDocumentSnapshot(
    val identity: RunLogDocumentIdentity?,
    val records: List<RunLogRecord>,
    val phase: RunSessionPhase,
    val report: RunReport?,
    val config: RunLogFormatConfig,
)

/**
 * Sole pure GLLOG1 byte-format authority shared by automatic export and manual share.
 *
 * Every returned physical row ends in exactly one LF. The formatter has no reverse parser and therefore cannot
 * become a product-success authority.
 */
class RunLogDocumentFormatter {
    /**
     * Formats the header, metadata, and records already present at automatic-export activation.
     *
     * @param identity Required accepted-run identity.
     * @param records Initial canonical records in commit order.
     * @param config Frozen timestamp setting.
     * @return Complete UTF-8/LF physical rows.
     */
    fun formatHeader(
        identity: RunLogDocumentIdentity,
        records: List<RunLogRecord>,
        config: RunLogFormatConfig,
    ): List<ByteArray> = buildList {
        add(row("GLLOG1"))
        add(row(meta(identity)))
        records.forEach { record -> add(formatRecord(record, config)) }
    }

    /**
     * Formats one canonical record.
     *
     * @param record Typed canonical record.
     * @param config Frozen timestamp setting.
     * @return One UTF-8 physical row ending in LF.
     */
    fun formatRecord(record: RunLogRecord, config: RunLogFormatConfig): ByteArray {
        val elapsed = if (config.showTimestamp) record.elapsedMs.coerceAtLeast(0L).toString() else "-"
        return row(
            "entry\torigin=" + record.origin.name +
                "\tlevel=" + record.level.name +
                "\telapsed_ms=" + elapsed +
                "\tmessage=" + escape(record.message),
        )
    }

    /**
     * Formats the terminal session, optional typed report, and completion marker.
     *
     * @param phase Final coordinator phase for this document.
     * @param report Complete typed report, or null for aborted/refused/transport-only terminals.
     * @return Ordered UTF-8/LF rows ending with `end\tcomplete=true`.
     */
    fun formatTrailer(phase: RunSessionPhase, report: RunReport?): List<ByteArray> = buildList {
        add(row("session\tphase=" + phase.name))
        if (report == null) {
            add(row("report\tpresent=false"))
        } else {
            add(row("report\tpresent=true"))
            add(row("report\trun_capability=" + report.runCapability.name))
            add(row("report\tdiagnostic_outcome=" + report.diagnosticOutcome.name))
            add(stageRow("backend_primitive", report.backendPrimitive))
            add(stageRow("root_script_dispatch", report.rootScriptDispatch))
            report.payloadItems.forEachIndexed { index, item -> add(payloadRow(index, item)) }
            add(stageRow("payload_overall", report.payloadOverall))
            report.finishItems.forEachIndexed { index, item -> add(finishRow(index, item)) }
            add(stageRow("finish_overall", report.finishOverall))
            add(stageRow("backend_cleanup", report.backendCleanup))
            add(row("report\tterminal=" + report.terminal.name))
            add(row("report\tproduct_success=" + report.isProductSuccess))
        }
        add(row("end\tcomplete=true"))
    }

    /**
     * Formats a complete immutable snapshot using the same row primitives as streaming export.
     *
     * @param snapshot Canonical snapshot captured under the coordinator lock.
     * @return Byte-identical manual document for a successful automatic export of the same snapshot.
     */
    fun format(snapshot: RunLogDocumentSnapshot): ByteArray {
        val rows = buildList {
            add(row("GLLOG1"))
            add(row(meta(snapshot.identity)))
            snapshot.records.forEach { record -> add(formatRecord(record, snapshot.config)) }
            addAll(formatTrailer(snapshot.phase, snapshot.report))
        }
        return ByteArrayOutputStream(rows.sumOf { row -> row.size }).use { output ->
            rows.forEach { row -> output.write(row) }
            output.toByteArray()
        }
    }

    /** @return Exact GLLOG1 metadata row body. */
    private fun meta(identity: RunLogDocumentIdentity?): String = if (identity == null) {
        "meta\trun_id=-\tgeneration=0\tbackend=-"
    } else {
        "meta\trun_id=" + escape(identity.runId) +
            "\tgeneration=" + identity.generation +
            "\tbackend=" + escape(identity.backendKey)
    }

    /** @return Exact typed stage row. */
    private fun stageRow(name: String, stage: StageResult): ByteArray = row(
        "stage\tname=" + name +
            "\tstate=" + stage.state.name +
            "\treason=" + stage.reason.name +
            "\texit_kind=" + stage.exitKind.name +
            "\texit_value=" + stage.exitValue,
    )

    /** @return Exact ordered Payload item row. */
    private fun payloadRow(index: Int, item: PayloadItemResult): ByteArray = row(
        "payload\tindex=" + index +
            "\titem_id=" + escape(item.itemId) +
            "\tstate=" + item.state.name +
            "\treason=" + item.reason.name +
            "\texit_kind=" + item.exitKind.name +
            "\texit_value=" + item.exitValue,
    )

    /** @return Exact ordered Finish item row. */
    private fun finishRow(index: Int, item: FinishItemResult): ByteArray = row(
        "finish\tindex=" + index +
            "\titem_id=" + escape(item.itemId) +
            "\tstate=" + item.state.name +
            "\treason=" + item.reason.name +
            "\texit_kind=" + item.exitKind.name +
            "\texit_value=" + item.exitValue,
    )

    /** @return Escaped cell with no physical CR, LF, or TAB. */
    private fun escape(value: String): String = buildString(value.length) {
        value.forEach { character ->
            when (character) {
                '\\' -> append("\\\\")
                '\r' -> append("\\r")
                '\n' -> append("\\n")
                '\t' -> append("\\t")
                else -> append(character)
            }
        }
    }

    /** @return UTF-8 bytes for one physical row and its final LF. */
    private fun row(body: String): ByteArray = (body + "\n").toByteArray(Charsets.UTF_8)
}
