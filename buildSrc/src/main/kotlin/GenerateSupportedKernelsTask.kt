import groovy.json.JsonSlurper
import org.gradle.api.DefaultTask
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction

abstract class GenerateSupportedKernelsTask : DefaultTask() {
    @get:InputFile
    abstract val profilesJson: RegularFileProperty

    @get:OutputFile
    abstract val generatedFile: RegularFileProperty

    @TaskAction
    fun generate() {
        val root = JsonSlurper().parse(profilesJson.get().asFile) as Map<*, *>
        require((root["schema_version"] as Number).toInt() == 1) {
            "unsupported kernel profile schema"
        }
        val profiles = (root["profiles"] as List<*>).map { value ->
            val profile = value as Map<*, *>
            val release = profile["release"] as String
            val fields = linkedMapOf<String, Long>()
            profile.forEach { (rawKey, rawValue) ->
                val key = rawKey as? String ?: return@forEach
                if (key != "release" && key != "execution" && rawValue is Number) {
                    fields[key] = rawValue.toLong()
                }
            }
            listOf("symbols", "struct_fields").forEach { group ->
                (profile[group] as? Map<*, *>)?.forEach { (rawKey, rawValue) ->
                    if (rawKey is String && rawValue is Number) {
                        fields[rawKey] = rawValue.toLong()
                    }
                }
            }
            release to fields
        }
        val output = buildString {
            appendLine("package com.ghostlock.app.domain.model")
            appendLine()
            appendLine("/** Generated from kernel_profiles.json; do not edit. */")
            appendLine("object SupportedKernels {")
            appendLine("    val UNAMES: Set<String> = setOf(")
            profiles.forEach { (release, _) -> appendLine("        \"${escape(release)}\",") }
            appendLine("    )")
            appendLine()
            appendLine("    val REQUIRES_SHIZUKU: Set<String> = setOf(")
            profiles.filter { (_, fields) -> fields["requires_shizuku"] == 1L }
                .forEach { (release, _) -> appendLine("        \"${escape(release)}\",") }
            appendLine("    )")
            appendLine()
            appendLine("    val BUILTIN: Map<String, Map<String, Long>> = mapOf(")
            profiles.forEach { (release, fields) ->
                appendLine("        \"${escape(release)}\" to mapOf(")
                fields.forEach { (key, value) ->
                    appendLine("            \"${escape(key)}\" to ${format(value)},")
                }
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

    private companion object {
        fun format(value: Long): String =
            if (value < 0) "${value}L" else "0x${value.toString(16)}L"

        fun escape(value: String): String =
            value.replace("\\", "\\\\").replace("\"", "\\\"")
    }
}
