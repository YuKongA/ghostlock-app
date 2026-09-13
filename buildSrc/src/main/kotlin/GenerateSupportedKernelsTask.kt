import groovy.json.JsonSlurper
import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction

abstract class GenerateSupportedKernelsTask : DefaultTask() {
    @get:InputDirectory
    abstract val profilesDirectory: DirectoryProperty

    @get:OutputFile
    abstract val generatedFile: RegularFileProperty

    @TaskAction
    fun generate() {
        val directory = profilesDirectory.get().asFile
        val index = JsonSlurper().parse(directory.resolve("index.json")) as Map<*, *>
        require((index["schema_version"] as Number).toInt() == 1) {
            "unsupported kernel profile schema"
        }
        val profiles = (index["profiles"] as List<*>).map { value ->
            val entry = value as Map<*, *>
            val profile = JsonSlurper().parse(directory.resolve(entry["file"] as String)) as Map<*, *>
            require((profile["schema_version"] as Number).toInt() == 1)
            val release = profile["release"] as String
            require(release == entry["release"]) { "profile index release mismatch: $release" }
            val fields = linkedMapOf<String, Long>()
            profile.forEach { (rawKey, rawValue) ->
                val key = rawKey as? String ?: return@forEach
                if (key != "schema_version" && key != "release" && key != "execution" && rawValue is Number) {
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
            appendLine("/** Generated from kernel_profiles/index.json; do not edit. */")
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
