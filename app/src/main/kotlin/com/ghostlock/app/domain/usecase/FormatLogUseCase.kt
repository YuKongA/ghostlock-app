package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.LogEntry
import com.ghostlock.app.domain.model.LogTone

class FormatLogUseCase {
    operator fun invoke(line: String): LogEntry {
        val text = stripAnsi(if (line.endsWith('\n')) line else "$line\n")
        val marker = text.getOrNull(1).takeIf { text.startsWith('[') && text.getOrNull(2) == ']' }
        val message = text.replace(leadingTags, "").removePrefix("=== ")
        val tone = when {
            isWriteRound(message) -> if (marker == '-' || marker == '!') LogTone.Error else LogTone.Progress
            marker == '+' -> LogTone.Success
            marker == '-' || marker == '!' -> LogTone.Error
            marker == '*' -> LogTone.Warning
            text.startsWith("error", ignoreCase = true) -> LogTone.Error
            text.startsWith("warning", ignoreCase = true) -> LogTone.Warning
            else -> LogTone.Default
        }
        return LogEntry(text, tone)
    }

    private fun isWriteRound(message: String): Boolean =
        listOf("W1", "W2", "W3", "Write 1").any(message::startsWith)

    private fun stripAnsi(value: String): String = value.replace(ansi, "")

    private companion object {
        val ansi = Regex("\\u001B\\[[;\\d]*m")
        val leadingTags = Regex("^(\\[[^]]+\\]\\s*)+")
    }
}
