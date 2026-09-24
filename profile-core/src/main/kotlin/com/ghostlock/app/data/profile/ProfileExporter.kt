package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueList
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.route.RouteKind
import java.io.File

/**
 * Serializes the bundled HOCON profiles into the v3 binary the native side
 * reads. It shares [ProfileMerger], [ProfileResolver] and
 * [NativeProfileDocument] with the app, so the exporter and the runtime can no
 * longer drift. The work list comes from `index.conf`; a missing/invalid entry
 * fails the export instead of silently dropping a profile.
 */
object ProfileExporter {
    @JvmStatic
    fun main(args: Array<String>) {
        require(args.size >= 3) {
            "usage: ProfileExporter <profilesDir> <outputDir> <expectedOutputDir>"
        }
        val srcDir = File(args[0]).canonicalFile
        val outDir = File(args[1]).canonicalFile
        val expectedOutDir = File(args[2]).canonicalFile
        require(srcDir.isDirectory) { "profiles dir does not exist: $srcDir" }
        validateOutputDir(srcDir, outDir, expectedOutDir)

        val index = parseFile(File(srcDir, "index.conf"), srcDir)
            ?: error("index.conf is missing or invalid")
        val profiles = index["profiles"]
        require(profiles is List<*>) { "index.conf profiles must be a list" }
        val entries = profiles.mapIndexed { index, raw ->
            raw.asValueMap() ?: error("index.conf profiles[$index] is not an object")
        }

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

        /* Stage into a sibling temp dir and swap in only on full success, so a
         * failure never leaves a half-deleted or partial output dir. */
        val staging = File(outDir.parentFile, "${outDir.name}.staging-${System.nanoTime()}")
        if (staging.exists()) staging.deleteRecursively()
        staging.mkdirs()

        var count = 0
        for (entry in entries) {
            val file = entry["file"] as? String ?: error("index entry missing 'file'")
            val release = entry["release"] as? String ?: error("index entry missing 'release'")
            /* Reference templates are not device profiles and are not exported. */
            if (release.endsWith("-template")) continue
            val parsed = parseFile(File(srcDir, file), srcDir) ?: error("cannot parse $file")
            val actualRelease = parsed["release"] as? String ?: error("$file has no release")
            require(actualRelease == release) { "index/file release mismatch for $file" }
            val route = routeNameOf(parsed, routes)
            val fallbackTo = fallbackOf(parsed)
            val merged = ProfileMerger.resolveMerged(
                deviceRelease = actualRelease,
                builtin = parsed,
                imported = null,
                overrides = null,
                tuningExecution = tuningExecution,
                pair = CpuPairView(0, 1),
                routePresets = routePresets,
            )
            val errors = ProfileResolver.validateMerged(merged, route, fallbackTo)
            if (errors.isNotEmpty()) {
                error("$file fails validation: ${errors.joinToString()}")
            }
            val bytes = NativeProfileDocument.from(
                release = actualRelease,
                route = RouteKind.fromToken(route)?.token,
                fallbackTo = RouteKind.fromToken(fallbackTo)?.token,
            ) { path -> ProfileResolver.nativeValue(merged, route, fallbackTo, path) }.toBinaryV3()
            File(staging, "$actualRelease.bin").writeBytes(bytes)
            count++
            println("exportKernelProfiles: $actualRelease (${bytes.size} bytes)")
        }

        /* Swap without ever deleting the previous output first: move it aside,
         * then move the staging dir in, and roll back if the move fails. */
        if (outDir.exists()) {
            val backup = File(outDir.parentFile, "${outDir.name}.bak-${System.nanoTime()}")
            if (!outDir.renameTo(backup)) error("cannot move current output aside: $outDir")
            if (!staging.renameTo(outDir)) {
                backup.renameTo(outDir)
                error("cannot move staging dir into place: $outDir (previous output restored)")
            }
            backup.deleteRecursively()
        } else if (!staging.renameTo(outDir)) {
            error("cannot move staging dir into place: $outDir")
        }
        println("exportKernelProfiles: $count profile(s) -> ${outDir.absolutePath}")
    }

    /**
     * Refuses any output that is not exactly the configured generated directory
     * (and never the source tree). [expectedDir] is the build directory the
     * Gradle task owns, so an attacker-supplied path can never cause a replace
     * of unrelated data even if it contains a `build` segment.
     */
    internal fun validateOutputDir(srcDir: File, outDir: File, expectedDir: File) {
        val src = srcDir.path
        val out = outDir.path
        require(outDir.parentFile != null && outDir.name.isNotEmpty()) {
            "invalid output dir: $outDir"
        }
        require(out == expectedDir.path) {
            "output dir must be the configured export dir $expectedDir, got $outDir"
        }
        val underBuild = out.contains("${File.separator}build${File.separator}") ||
            outDir.parentFile!!.name == "build"
        require(underBuild) { "output dir must live under a build directory: $outDir" }
        require(out != src) { "output dir must not be the profiles dir: $outDir" }
        require(!out.startsWith(src + File.separator)) {
            "output dir must not be inside the profiles dir: $outDir"
        }
        require(!src.startsWith(out + File.separator)) {
            "output dir must not contain the profiles dir: $outDir"
        }
        require(!out.contains("${File.separator}app${File.separator}src${File.separator}")) {
            "refusing to write into the source tree: $outDir"
        }
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
