import org.gradle.api.DefaultTask
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction
import java.io.File

abstract class GenerateSupportedKernelsTask : DefaultTask() {
    @get:InputFiles
    abstract val offsetHeaders: ConfigurableFileCollection

    @get:InputFile
    abstract val sharedHeader: RegularFileProperty

    @get:OutputFile
    abstract val generatedFile: RegularFileProperty

    @TaskAction
    fun generate() {
        val macros = parseStructMacros(sharedHeader.get().asFile.readText())
        val names = mutableListOf<String>()
        val builtins = linkedMapOf<String, Map<String, Long>>()
        offsetHeaders.forEach { header ->
            val parsed = parseKernelEntries(header, macros)
            names += parsed.names
            builtins.putAll(parsed.entries)
        }
        val output = buildString {
            appendLine("package com.ghostlock.app.domain.model")
            appendLine()
            appendLine("/** Generated from kernel offset headers; do not edit. */")
            appendLine("object SupportedKernels {")
            appendLine("    val UNAMES: Set<String> = setOf(")
            names.distinct().forEach { appendLine("        \"${escape(it)}\",") }
            appendLine("    )")
            appendLine()
            appendLine("    /** Built-in release -> field -> value (STRUCT_OFFSETS_* macros expanded). */")
            appendLine("    val BUILTIN: Map<String, Map<String, Long>> = mapOf(")
            builtins.forEach { (release, fields) ->
                appendLine("        \"${escape(release)}\" to mapOf(")
                fields.forEach { (key, value) -> appendLine("            \"${escape(key)}\" to ${format(value)},") }
                appendLine("        ),")
            }
            appendLine("    )")
            appendLine("}")
        }
        generatedFile.get().asFile.apply {
            parentFile.mkdirs()
            writeText(output)
        }
    }

    private data class ParsedKernelEntries(
        val names: List<String>,
        val entries: Map<String, Map<String, Long>>,
    )

    private companion object {
        val offsetField = Regex("\\.([A-Za-z0-9_]+)\\s*=\\s*(0[xX][0-9A-Fa-f]+|-?\\d+)")

        fun parseValue(text: String): Long =
            if (text.startsWith("0x", ignoreCase = true)) text.substring(2).toLong(16) else text.toLong()

        fun format(value: Long): String = if (value < 0) "${value}L" else "0x${value.toString(16)}L"

        fun escape(value: String): String = value.replace("\\", "\\\\").replace("\"", "\\\"")

        fun parseStructMacros(text: String): Map<String, Map<String, Long>> {
            val macros = mutableMapOf<String, Map<String, Long>>()
            val lines = text.lines()
            var index = 0
            while (index < lines.size) {
                val match = Regex("#define\\s+(STRUCT_OFFSETS_[A-Za-z0-9_]+)\\s*(.*)").matchEntire(lines[index])
                if (match == null) {
                    index++
                    continue
                }
                val name = match.groupValues[1]
                var body = match.groupValues[2]
                while (lines[index].trimEnd().endsWith("\\") && index + 1 < lines.size) {
                    index++
                    body += " ${lines[index]}"
                }
                macros[name] = offsetField.findAll(body).associate { it.groupValues[1] to parseValue(it.groupValues[2]) }
                index++
            }
            return macros
        }

        fun parseKernelEntries(header: File, macros: Map<String, Map<String, Long>>): ParsedKernelEntries {
            val text = header.readText()
            val names = mutableListOf<String>()
            val entries = linkedMapOf<String, Map<String, Long>>()
            for (match in Regex("OFFSETS_ENTRY\\(\\s*\"([^\"]+)\"").findAll(text)) {
                val release = match.groupValues[1]
                val body = text.substring(match.range.last + 1).substringBefore("\n),")
                val fields = linkedMapOf<String, Long>()
                Regex("STRUCT_OFFSETS_[A-Za-z0-9_]+").find(body)?.value?.let { macros[it]?.let(fields::putAll) }
                offsetField.findAll(body).forEach { fields[it.groupValues[1]] = parseValue(it.groupValues[2]) }
                names += release
                entries[release] = fields
            }
            return ParsedKernelEntries(names, entries)
        }
    }
}
