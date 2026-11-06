package com.ghostlock.app.ui.screen.run

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle

/**
 * Converts the approved foreground/reset SGR subset into Compose spans.
 *
 * @param message Validated log body; malformed direct-call control sequences are discarded defensively.
 * @param defaultColor Severity-derived color restored by SGR 0 and 39.
 * @return Plain text plus foreground spans; no cursor, erase, link, or style command is interpreted.
 */
internal fun ansiRunLogText(message: String, defaultColor: Color): AnnotatedString = buildAnnotatedString {
    var index = 0
    var segmentStart = 0
    var color = defaultColor
    while (index < message.length) {
        if (message[index].code != ESCAPE_CODE) {
            index += 1
            continue
        }
        if (segmentStart < index) {
            appendColored(message.substring(segmentStart, index), color)
        }
        val sgr = parseSgr(message, index)
        if (sgr == null) {
            index += 1
            segmentStart = index
            continue
        }
        sgr.parameters.forEach { parameter ->
            color = when (parameter) {
                0, 39 -> defaultColor
                else -> ansiForeground(parameter) ?: color
            }
        }
        index = sgr.endExclusive
        segmentStart = index
    }
    if (segmentStart < message.length) {
        appendColored(message.substring(segmentStart), color)
    }
}

/** Appends one literal text run with its resolved foreground. */
private fun AnnotatedString.Builder.appendColored(value: String, color: Color) {
    withStyle(SpanStyle(color = color)) { append(value) }
}

/** @return One approved SGR sequence, or null when the direct-call input is malformed/unsupported. */
private fun parseSgr(message: String, start: Int): ParsedSgr? {
    if (start + 3 >= message.length || message[start + 1] != '[') return null
    val limit = minOf(message.length, start + MAX_SGR_BYTES)
    var end = start + 2
    while (end < limit && message[end] != 'm') end += 1
    if (end >= limit || message[end] != 'm') return null
    val parameters = message.substring(start + 2, end).split(';')
    if (parameters.size !in 1..MAX_SGR_PARAMETERS) return null
    val values = parameters.map { parameter ->
        if (parameter.length !in 1..MAX_SGR_PARAMETER_DIGITS || !parameter.all(Char::isDigit)) return null
        parameter.toIntOrNull()?.takeIf { value -> value in APPROVED_SGR_PARAMETERS } ?: return null
    }
    return ParsedSgr(values, end + 1)
}

/** @return Explicit color for one standard or bright ANSI foreground parameter. */
private fun ansiForeground(parameter: Int): Color? = when (parameter) {
    30 -> Color(0xFF000000)
    31 -> Color(0xFFFF5252)
    32 -> Color(0xFF4CAF50)
    33 -> Color(0xFFFFD740)
    34 -> Color(0xFF42A5F5)
    35 -> Color(0xFFCE93D8)
    36 -> Color(0xFF4DD0E1)
    37 -> Color(0xFFE0E0E0)
    90 -> Color(0xFF757575)
    91 -> Color(0xFFFF8A80)
    92 -> Color(0xFF69F0AE)
    93 -> Color(0xFFFFFF8D)
    94 -> Color(0xFF82B1FF)
    95 -> Color(0xFFE1BEE7)
    96 -> Color(0xFF84FFFF)
    97 -> Color(0xFFFFFFFF)
    else -> null
}

/** Parsed SGR parameters and the first following character. */
private data class ParsedSgr(
    val parameters: List<Int>,
    val endExclusive: Int,
)

private val APPROVED_SGR_PARAMETERS: Set<Int> = setOf(0, 39) + (30..37) + (90..97)
private const val ESCAPE_CODE: Int = 0x1b
private const val MAX_SGR_BYTES: Int = 16
private const val MAX_SGR_PARAMETERS: Int = 4
private const val MAX_SGR_PARAMETER_DIGITS: Int = 3
