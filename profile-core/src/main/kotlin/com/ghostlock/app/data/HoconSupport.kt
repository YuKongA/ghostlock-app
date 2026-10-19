package com.ghostlock.app.data

import com.typesafe.config.ConfigFactory
import com.typesafe.config.ConfigParseOptions
import com.typesafe.config.ConfigSyntax

/**
 * HOCON loader for every configuration source. Documents are parsed with the
 * HOCON grammar (comments, `${variables}`, trailing commas; file includes are
 * expanded by [AssetConfigLoader] before parsing) straight into the value
 * model (LinkedHashMap / ArrayList / scalars) and rendered back from it.
 */
object HoconSupport {
    private val parseOptions = ConfigParseOptions.defaults()
        .setSyntax(ConfigSyntax.CONF)
        .setAllowMissing(false)

    /**
     * Parses an object document, or an array document wrapped so HOCON sees a
     * value (the offsets store keeps its top-level array shape).
     */
    fun parseValue(text: String): Any? {
        val isArray = text.trimStart().startsWith("[")
        val source = if (isArray) "value = $text" else text
        val config = runCatching {
            ConfigFactory.parseString(source, parseOptions).resolve()
        }.getOrNull() ?: return null
        val unwrapped = config.root().unwrapped()
        val value = if (isArray) unwrapped["value"] else unwrapped
        return normalize(value)
    }

    private fun normalize(value: Any?): Any? = when (value) {
        is Map<*, *> -> linkedMapOf<String, Any?>().apply {
            value.forEach { (key, item) -> put(key.toString(), normalize(item)) }
        }

        is List<*> -> arrayListOf<Any?>().apply {
            value.forEach { item -> add(normalize(item)) }
        }

        else -> value
    }

    /** Renders the value model as HOCON text, in the bundled files' style. */
    fun render(value: Any?): String = buildString { renderValue(this, value, 0) }

    private fun renderValue(out: StringBuilder, value: Any?, indent: Int) {
        when (value) {
            is Map<*, *> -> renderMap(out, value, indent)
            is List<*> -> renderListDocument(out, value, indent)
            else -> out.append(literal(value))
        }
    }

    /** Top-level arrays (the offsets store) must keep their brackets. */
    private fun renderListDocument(out: StringBuilder, list: List<*>, indent: Int) {
        if (list.isEmpty()) {
            out.append("[]")
            return
        }
        out.append("[\n")
        renderListElements(out, list, indent + 1)
        out.append('\n').append("  ".repeat(indent)).append(']')
    }

    private fun renderMap(out: StringBuilder, map: Map<*, *>, indent: Int) {
        val pad = "  ".repeat(indent)
        var first = true
        for ((key, value) in map) {
            if (!first) out.append('\n')
            first = false
            val field = keyName(key.toString())
            when (value) {
                is Map<*, *> -> {
                    if (value.isEmpty()) {
                        out.append(pad).append(field).append(" = {}")
                    } else {
                        out.append(pad).append(field).append(" {\n")
                        renderMap(out, value, indent + 1)
                        out.append('\n').append(pad).append('}')
                    }
                }

                is List<*> -> {
                    if (value.isEmpty()) {
                        out.append(pad).append(field).append(" = []")
                    } else {
                        out.append(pad).append(field).append(" = [\n")
                        renderListElements(out, value, indent + 1)
                        out.append('\n').append(pad).append(']')
                    }
                }

                else -> out.append(pad).append(field).append(" = ").append(literal(value))
            }
        }
    }

    /** Array syntax uses newline separators below the first element. */
    private fun renderListElements(out: StringBuilder, list: List<*>, indent: Int) {
        val pad = "  ".repeat(indent)
        list.forEachIndexed { index, item ->
            if (index > 0) out.append('\n')
            when (item) {
                is Map<*, *> -> {
                    if (isFlatMap(item)) {
                        out.append(pad).append('{')
                        var first = true
                        for ((key, value) in item) {
                            if (!first) out.append(',')
                            first = false
                            out.append(' ').append(keyName(key.toString()))
                                .append(" = ").append(literal(value))
                        }
                        out.append(" }")
                    } else {
                        out.append(pad).append('{').append('\n')
                        renderMap(out, item, indent + 1)
                        out.append('\n').append(pad).append('}')
                    }
                }

                else -> out.append(pad).append(literal(item))
            }
        }
    }

    private fun isFlatMap(map: Map<*, *>): Boolean = map.values.none {
        it is Map<*, *> || it is List<*>
    }

    private fun keyName(key: String): String =
        if (key.matches(Regex("[A-Za-z0-9_\\-]+"))) key else quote(key)

    private fun literal(value: Any?): String = when (value) {
        null -> "null"
        is Boolean -> value.toString()
        is Number -> value.toString()
        is String -> quote(value)
        else -> quote(value.toString())
    }

    private fun quote(value: String): String = buildString {
        append('"')
        for (character in value) {
            when (character) {
                '"' -> append("\\\"")
                '\\' -> append("\\\\")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                '\t' -> append("\\t")
                '\b' -> append("\\b")
                '\u000C' -> append("\\f")
                else -> if (character < ' ') {
                    append("\\u").append(character.code.toString(16).padStart(4, '0'))
                } else {
                    append(character)
                }
            }
        }
        append('"')
    }
}
