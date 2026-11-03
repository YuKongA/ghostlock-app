package com.ghostlock.app.data

import android.content.Context

/**
 * Reads a configuration document from assets and expands its HOCON-style
 * `include "file.conf"` directives (relative to the including file, nested,
 * cycle-safe). The expanded text is then parsed by [HoconSupport].
 */
internal class AssetConfigLoader(private val context: Context) {
    fun load(relativePath: String): String = expand(relativePath, linkedSetOf())

    private fun expand(relativePath: String, visiting: MutableSet<String>): String {
        if (!visiting.add(relativePath)) return ""
        val text = runCatching {
            context.assets.open(relativePath).bufferedReader().use { it.readText() }
        }.getOrElse {
            visiting.remove(relativePath)
            return ""
        }
        val baseDir = relativePath.substringBeforeLast('/', "")
        val expanded = StringBuilder()
        for (line in text.lineSequence()) {
            val trimmed = line.trim()
            val target = when {
                trimmed.startsWith("#") || trimmed.startsWith("//") -> null
                trimmed.startsWith("include ") ->
                    trimmed.removePrefix("include ").trim()
                        .trim('"')
                        .takeIf { it.isNotEmpty() }

                else -> null
            }
            if (target == null) {
                expanded.append(line).append('\n')
            } else {
                val resolved = if (baseDir.isEmpty()) target else "$baseDir/$target"
                expanded.append(expand(resolved, visiting)).append('\n')
            }
        }
        visiting.remove(relativePath)
        return expanded.toString()
    }
}
