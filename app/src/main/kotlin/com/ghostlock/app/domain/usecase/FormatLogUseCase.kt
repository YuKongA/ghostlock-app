package com.ghostlock.app.domain.usecase

import com.ghostlock.app.domain.model.LogEntry
import com.ghostlock.app.domain.model.LogTone

class FormatLogUseCase {
    operator fun invoke(line: String): LogEntry {
        val text = stripAnsi(if (line.endsWith('\n')) line else "$line\n")
        // a source tag such as [ksu] hides the marker that follows it
        val body = text.replaceFirst(sourceTag, "")
        val marker = body.getOrNull(1).takeIf { body.startsWith('[') && body.getOrNull(2) == ']' }
        val message = body.replace(leadingTags, "").removePrefix("=== ")
        val tagged = text
            .removePrefix("<-> ")
            .removePrefix("<b> ")
            .removePrefix("<s> ")
            .removePrefix("<k> ")
        val tone = when {
            /* Explicit source tags keep Kotlin/Shizuku lines apart from the
             * native "[x]" markers. "<->" is a Kotlin error; "<b>" a module
             * hand-off (blue, unless the connection succeeded -> green);
             * "<s>" Shizuku plumbing (red when it reports a failure). */
            text.startsWith("<->") -> LogTone.Error
            text.startsWith("<b>") ->
                if (tagged.contains("connected") && !tagged.contains("disconnected")) {
                    LogTone.Success
                } else {
                    LogTone.Progress
                }

            text.startsWith("<s>") -> if (looksLikeError(tagged)) LogTone.Error else LogTone.Shizuku
            text.startsWith("<k>") ->
                if (tagged.startsWith("result:")) resultTone(tagged) else LogTone.Kotlin

            tagged.startsWith("result:") -> resultTone(tagged)
            isWriteRound(message) -> if (marker == '-' || marker == '!') LogTone.Error else LogTone.Progress
            marker == '+' -> LogTone.Success
            marker == '-' || marker == '!' -> LogTone.Error
            marker == '*' -> LogTone.Warning
            body.startsWith("error", ignoreCase = true) -> LogTone.Error
            body.startsWith("warning", ignoreCase = true) -> LogTone.Warning
            else -> LogTone.Default
        }
        return LogEntry(text, tone)
    }

    private fun isWriteRound(message: String): Boolean = listOf("W1", "W2", "W3", "Write 1").any(message::startsWith)

    private fun resultTone(text: String): LogTone =
        if (listOf("failed", "unsupported", "cancelled").any(text::contains)) LogTone.Error else LogTone.Success

    private fun looksLikeError(text: String): Boolean {
        val lower = text.lowercase()
        return lower.startsWith("error") ||
            listOf("fail", "cannot", "unable", "denied", "refus").any(lower::contains)
    }

    private fun stripAnsi(value: String): String = value.replace(ansi, "")

    private companion object {
        val ansi = Regex("\\u001B\\[[;\\d]*m")
        val sourceTag = Regex("^\\[[a-z]+]\\s+")
        val leadingTags = Regex("^(\\[[^]]+]\\s*)+")
    }
}
