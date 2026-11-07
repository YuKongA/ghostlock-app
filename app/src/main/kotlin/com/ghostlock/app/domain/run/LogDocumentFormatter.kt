package com.ghostlock.app.domain.run

/** Formatting options frozen at the accepted-run boundary. */
data class LogFormatConfig(val showTimestamp: Boolean)

/** Accepted-run identity; null is reserved for an idle manual snapshot. */
data class LogDocumentIdentity(val runId: String, val generation: Long, val backendKey: String)

/** Sole GLLOG2 physical-row authority; all rows are UTF-8 with exactly one trailing LF. */
class LogDocumentFormatter {
    /** Encodes the header and identity without copying any complete document. */
    fun formatHeader(identity: LogDocumentIdentity?): List<ByteArray> = listOf(
        row("GLLOG2"),
        row(if (identity == null) {
            "meta\trun_id=-\tgeneration=0\tbackend=-"
        } else {
            "meta\trun_id=" + escape(identity.runId) +
                "\tgeneration=" + identity.generation + "\tbackend=" + escape(identity.backendKey)
        }),
    )

    /** Encodes one committed record once for all enabled sinks. */
    fun formatRecord(record: LogRecord, config: LogFormatConfig): ByteArray {
        require(record.commitId > 0L) { "log commit id must be positive" }
        val elapsed = if (config.showTimestamp) record.elapsedMs.coerceAtLeast(0L).toString() else "-"
        return row(
            "entry\tcommit_id=" + record.commitId +
                "\torigin=" + record.origin.name +
                "\tlevel=" + record.level.name +
                "\telapsed_ms=" + elapsed +
                "\tmessage=" + escape(record.message),
        )
    }

    /** Encodes the terminal marker without a hidden typed-report trailer. */
    fun formatTrailer(phase: RunSessionPhase): List<ByteArray> = listOf(
        row("session\tphase=" + phase.name),
        row("end\tterminal_received=true"),
    )

    /** Escapes field separators and physical newlines without interpreting approved SGR bytes. */
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

    /** @return Exactly one physical UTF-8 row with a final LF. */
    private fun row(body: String): ByteArray = (body + "\n").toByteArray(Charsets.UTF_8)
}
