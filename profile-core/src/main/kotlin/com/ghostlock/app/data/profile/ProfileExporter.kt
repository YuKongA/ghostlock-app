package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.route.RouteKind
import java.io.File

/**
 * Serializes the bundled HOCON profiles into the v2 binary the native side
 * reads. It shares [ProfileMerger], [ProfileResolver] and
 * [NativeProfileDocument] with the app, so the exporter and the runtime can no
 * longer drift.
 */
object ProfileExporter {
    @JvmStatic
    fun main(args: Array<String>) {
        require(args.size >= 2) { "usage: ProfileExporter <profilesDir> <outputDir>" }
        val srcDir = File(args[0])
        val outDir = File(args[1])
        if (outDir.exists()) outDir.deleteRecursively()
        outDir.mkdirs()

        val routes = RouteKind.entries.map { it.token }
        val tuningExecution = parseFile(File(srcDir, "execution-tuning.conf"), srcDir)
            ?.get("execution").asValueMap()
        val routePresets = routes.mapNotNull { route ->
            parseFile(File(srcDir, "execution-${route.replace('_', '-')}.conf"), srcDir)
                ?.get("execution").asValueMap()
                ?.get("routes").asValueMap()
                ?.get(route).asValueMap()
                ?.let { route to it }
        }.toMap()

        var count = 0
        srcDir.listFiles { file -> file.isFile && file.name.endsWith(".conf") }
            ?.sortedBy { it.name }
            ?.forEach { file ->
                val parsed = parseFile(file, srcDir) ?: return@forEach
                val release = parsed["release"] as? String ?: return@forEach
                val route = routeNameOf(parsed, routes)
                val fallbackTo = fallbackOf(parsed)
                val merged = ProfileMerger.resolveMerged(
                    deviceRelease = release,
                    builtin = parsed,
                    imported = null,
                    overrides = null,
                    tuningExecution = tuningExecution,
                    pair = CpuPairView(0, 1),
                    routePresets = routePresets,
                )
                val bytes = NativeProfileDocument.from(
                    release = release,
                    route = RouteKind.fromToken(route)?.token,
                    fallbackTo = RouteKind.fromToken(fallbackTo)?.token,
                ) { path -> ProfileResolver.nativeValue(merged, route, fallbackTo, path) }.toBinary()
                File(outDir, "$release.bin").writeBytes(bytes)
                count++
                println("exportKernelProfiles: $release (${bytes.size} bytes)")
            }
        println("exportKernelProfiles: $count profile(s) -> ${outDir.absolutePath}")
    }

    private fun routeNameOf(profile: Map<*, *>, routes: List<String>): String? =
        when (val route = profile["route"]) {
            is String -> route.takeIf { it.isNotEmpty() && it != "null" }
            is Map<*, *> -> route.keys.filterIsInstance<String>().firstOrNull { it in routes }
            else -> null
        }

    private fun fallbackOf(profile: Map<*, *>): String? {
        val to = ((profile["fallback"] as? Map<*, *>)?.get("to") as? String)
            ?.takeIf { it.isNotEmpty() && it != "null" }
        if (to != null) return to
        return (profile["fallback_to"] as? String)?.takeIf { it.isNotEmpty() && it != "null" }
    }

    private fun parseFile(file: File, baseDir: File): ValueMap? {
        if (!file.isFile) return null
        return runCatching { HoconSupport.parseValue(expand(file, baseDir, linkedSetOf())).asValueMap() }
            .getOrNull()
    }

    private fun expand(file: File, baseDir: File, visiting: MutableSet<String>): String {
        val name = file.absolutePath
        if (!visiting.add(name)) return ""
        val text = runCatching { file.readText() }.getOrElse {
            visiting.remove(name)
            return ""
        }
        val builder = StringBuilder()
        for (line in text.lineSequence()) {
            val trimmed = line.trim()
            val target = when {
                trimmed.startsWith("#") || trimmed.startsWith("//") -> null
                trimmed.startsWith("include ") ->
                    trimmed.removePrefix("include ").trim().trim('"').takeIf { it.isNotEmpty() }

                else -> null
            }
            if (target == null) {
                builder.append(line).append('\n')
            } else {
                builder.append(expand(File(baseDir, target), baseDir, visiting)).append('\n')
            }
        }
        visiting.remove(name)
        return builder.toString()
    }
}
