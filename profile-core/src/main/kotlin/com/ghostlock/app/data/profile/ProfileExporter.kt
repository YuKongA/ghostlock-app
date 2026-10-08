package com.ghostlock.app.data.profile

import com.ghostlock.app.data.AvailablePriority
import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.stepNames
import com.ghostlock.app.data.declaredCombinations
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.mutableChild
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
        require(args.size >= 4) {
            "usage: ProfileExporter <profilesDir> <outputDir> <expectedOutputDir> " +
                "<buildDir>"
        }
        val srcDir = File(args[0]).canonicalFile
        val outDir = File(args[1]).canonicalFile
        val expectedOutDir = File(args[2]).canonicalFile
        /* The build directory is PASSED IN (the Gradle task owns it) instead of
         * being guessed from a directory name: the safety property is containment,
         * not spelling.
         * HISTORY: the build tree used to be RELOCATED and symlinked for iCloud
         * (build.nosync + a build symlink). The current rule is that all build output
         * lives in the repository-internal real build/ with no links; the argument
         * stays passed in because containment is still the property being checked. */
        val buildDir = File(args[3]).canonicalFile
        require(srcDir.isDirectory) { "profiles dir does not exist: $srcDir" }
        validateOutputDir(srcDir, outDir, expectedOutDir, buildDir)

        val index = parseFile(File(srcDir, "index.conf"), srcDir)
            ?: error("index.conf is missing or invalid")
        val profiles = index["profiles"]
        require(profiles is List<*>) { "index.conf profiles must be a list" }
        val entries = profiles.mapIndexed { index, raw ->
            raw.asValueMap() ?: error("index.conf profiles[$index] is not an object")
        }

        val routes = RouteKind.entries.map { it.token }
        val tuningExecution = parseFile(File(srcDir, "execution-tuning.conf"), srcDir, normalize = true)
            ?.get("execution").asValueMap()
        val routePresets = routes.mapNotNull { route ->
            parseFile(File(srcDir, "execution-${route.replace('_', '-')}.conf"), srcDir, normalize = true)
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
            /* Reference templates are not device profiles and are not exported.
             * The repository currently ships NO *-template.conf at all (they were
             * deleted once the general profiles replaced them); the guard is kept
             * as regression protection, so a template added later is skipped
             * automatically instead of being exported as a device profile. */
            if (release.endsWith("-template")) continue
            val parsed = parseFile(File(srcDir, file), srcDir, normalize = true)
                ?: error("cannot parse $file")
            val actualRelease = parsed["release"] as? String ?: error("$file has no release")
            require(actualRelease == release) { "index/file release mismatch for $file" }
            val route = routeNameOf(parsed, routes)
            val merged = ProfileMerger.resolveMerged(
                deviceRelease = actualRelease,
                builtin = parsed,
                imported = null,
                overrides = null,
                tuningExecution = tuningExecution,
                pair = CpuPairView(0, 1),
                routePresets = routePresets,
            )
            /* M5 / (C): the DECLARATION decides the default backend, through the
             * SAME authority the App uses (AvailablePriority) - not a second rule.
             * Without this injection the merged document carries no
             * `backend.kind`, NativeProfileDocument.from falls back to
             * BackendKind.Default (cve_2026_43499) and the exporter emits a
             * route-ful 43499 document while the App emits the DECLARED one: two
             * paths that must stay value-equivalent (M5). The gap was latent until
             * the App started following the declaration. A document that declares
             * nothing keeps the previous behaviour. */
            /* The DECLARATION must be captured BEFORE normalization (the same pattern
             * as window 1's loadEntryWithDeclaration): parseFile(normalize = true)
             * replaces the map with the runtime form, which DROPS `available` - so
             * reading it from `merged` yields null and every injection below is
             * silently skipped. A second, non-normalized parse is the minimal fix and
             * leaves parseFile's signature untouched. The require is deliberate: a
             * missing declaration must fail loudly instead of skipping silently. */
            /* The raw (un-normalized) parse keeps the R3 wrapper `ghostlock { ... }`,
             * so `available` sits UNDER it: reading the top level returns null and the
             * whole injection silently skipped (measured: declaredSection=false).
             * Reuse the official unwrap instead of hand-rolling one. */
            val rawRoot = parseFile(File(srcDir, file), srcDir, normalize = false)
            val declaredSection = HoconSupport.unwrapProfileDocument(rawRoot)
                .asValueMap()?.get("available")
            /* A profile may legitimately declare NOTHING (the "none" class): then the
             * exporter keeps the historical Default behaviour and the injections below
             * are correctly skipped. But when an `available` key IS present it must hold
             * at least one backend - an empty capture means the object-form declaration
             * was lost (normalization replaces the map and drops `available`), which must
             * fail loudly instead of silently skipping the injections. */
            val declaration = declaredSection.asValueMap() ?: ValueMap()
            if (declaredSection != null) {
                require(declaration.isNotEmpty()) {
                    actualRelease + ": available is present but captured empty " +
                        "(did normalization eat it?)"
                }
            }
            AvailablePriority.orderedBackends(declaration).firstOrNull()?.let { token ->
                merged.mutableChild("backend")["kind"] = token
            }
            /* M5: the exporter must SELECT exactly like the App does. The declaration
             * decides the default backend (AvailablePriority - the SAME single
             * authority the App uses), and the token of that backend's ONLY available
             * catalogue row is carried in backend.steps - the INTERNAL selection
             * carrier, not a wire field - which NativeProfileDocument.from turns into
             * the combination that owns the terminal. Without it the exporter kept the
             * catalogue default terminal (root_child) while the App emitted the
             * declared one (umh_forward): two paths one byte apart.
             * UNIQUENESS: two available rows for this backend would be ambiguous, and
             * taking the first silently is the very bug class this batch kept hitting
             * (a missing/incorrect match that quietly changes nothing), so it fails
             * loudly instead of skipping.
             * CANDIDATE FOR UNIFICATION: the App reverse-resolves through
             * CombinationCatalog.fromDerived(backend, steps, terminal) from the UI
             * selection; this is the profile-core equivalent (same authority, no UI
             * selection available in the exporter).
             * A document that declares nothing keeps the previous behaviour. */
            val defaultBackend = AvailablePriority.orderedBackends(declaration).firstOrNull()
            if (defaultBackend != null) {
                val kind = requireNotNull(BackendKind.resolve(defaultBackend)) {
                    "the declared default backend is not a known kind: " + defaultBackend
                }
                val specs = CombinationCatalog.forBackend(kind).filter { it.available }
                require(specs.size == 1) {
                    "the declared default backend has " + specs.size +
                        " available combinations, expected exactly one: " + kind.token
                }
            }
            val errors = ProfileResolver.validateMerged(merged, route)
            if (errors.isNotEmpty()) {
                error("$file fails validation: ${errors.joinToString()}")
            }
            /* ORDER MATTERS: validateMerged normalizes the owner and DROPS the
             * removed M5 key `backend.steps` (while the known `backend.kind`
             * survives). Writing the selection carrier before it therefore vanished
             * silently - measured: kind=cve_2026_43284 but steps=null - and the
             * builder produced no combination. The carrier is written AFTER
             * validation, and the require proves it is visible on the very map the
             * builder reads (a silent no-op here is exactly the bug class this batch
             * kept hitting).
             * CANDIDATE FOR UNIFICATION: the App reverse-resolves through
             * CombinationCatalog.fromDerived(backend, steps, terminal) from the UI
             * selection; this is the profile-core equivalent (same authority, no UI
             * selection available in the exporter). */
            /* Expected terminal of the declared default, derived from the SAME catalogue
             * row (zero hard-coding); used by the post-construction check below. */
            var expectedTerminal: String? = null
            if (defaultBackend != null) {
                val kind = requireNotNull(BackendKind.resolve(defaultBackend)) {
                    "the declared default backend is not a known kind: " + defaultBackend
                }
                val specs = CombinationCatalog.forBackend(kind).filter { it.available }
                require(specs.size == 1) {
                    "the declared default backend has " + specs.size +
                        " available combinations, expected exactly one: " + kind.token
                }
                expectedTerminal = specs.first().terminal.token
                merged.mutableChild("backend")["steps"] = specs.first().token
                require(merged.getValueAt("backend.steps") == specs.first().token) {
                    actualRelease + ": the selection carrier did not survive: " +
                        merged.getValueAt("backend.steps")
                }
            }
            val document = NativeProfileDocument.from(
                release = actualRelease,
                route = RouteKind.resolve(RouteKind.normalize(route))?.token,
                value = { path -> ProfileResolver.nativeValue(merged, route, path) },
                text = { path -> ProfileResolver.nativeText(merged, path) },
                bool = { path -> ProfileResolver.nativeBool(merged, path) },
                /* M4: the declared step queue is an array of maps; read it raw. */
                raw = { path -> merged.getValueAt(path) },
            )
            /* PERMANENT SELF-DIAGNOSTIC for the selection carrier: when the carrier
             * IS set (the declared-default path; skipped for the "none" class) the
             * builder MUST turn it into a combination - that combination owns the
             * terminal, and a silent null here is exactly the "wrote it but nothing
             * happened" bug class this batch kept hitting. The message carries all
             * four discriminators (carrier, combination, terminal) so one failure
             * localises the fault: read path, selection, or downstream adapter. */

            if (expectedTerminal != null) {
                /* The declared-default path DOES produce the right combination: the
                 * diagnostic equality check against the only available 43284 row
                 * (umh => terminal umh_forward) passed on a DIFFERENT tree state, so this
                 * guard now checks the VALUE against the same catalogue row instead of
                 * merely non-null: firing means the selection/resolution is wrong,
                 * passing while the encoded bytes stay short means the fault is below
                 * the document (encoder/wire writing). Zero hard-coding: the expected
                 * terminal comes from the same row used for the carrier. */
                require(document.combination?.terminal?.token == expectedTerminal) {
                    actualRelease + ": document terminal=" +
                        document.combination?.terminal?.token + " expected=" + expectedTerminal +
                        " ; carrier=" + merged.getValueAt("backend.steps") +
                        " ; combination=" + document.combination?.token
                }
            }


            /* GLKv3-4: the exporter emits the production v3 MessagePack wire;
             * native is v3-only (S4 R2c). */
            val bytes = Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document))
            File(staging, "$actualRelease.bin").writeBytes(bytes)
            count++
            println("exportProfiles: $actualRelease (${bytes.size} bytes)")
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
        println("exportProfiles: $count profile(s) -> ${outDir.absolutePath}")
    }

    /**
     * Refuses any output that is not exactly the configured generated directory
     * (and never the source tree). [expectedDir] is the build directory the
     * Gradle task owns, so an attacker-supplied path can never cause a replace
     * of unrelated data even if it contains a `build` segment.
     */
    internal fun validateOutputDir(
        srcDir: File,
        outDir: File,
        expectedDir: File,
        buildDir: File,
    ) {
        val src = srcDir.path
        val out = outDir.path
        val build = buildDir.path
        require(outDir.parentFile != null && outDir.name.isNotEmpty()) {
            "invalid output dir: $outDir"
        }
        require(out == expectedDir.path) {
            "output dir must be the configured export dir $expectedDir, got $outDir"
        }
        /* Containment in the BUILD DIRECTORY this run was given (no name
         * assumption): writing anywhere outside the project build tree stays
         * refused, which is the property the rule exists for. */
        require(out == build || out.startsWith(build + File.separator)) {
            "output dir must live under the build directory " + buildDir + ", got " + outDir
        }
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

    private fun parseFile(file: File, baseDir: File, normalize: Boolean = false): ValueMap? {
        if (!file.isFile) return null
        return runCatching {
            val value = HoconSupport.parseValue(expand(file, baseDir, linkedSetOf())).asValueMap()
            if (normalize && value != null) ProfileLayout.applyNormalize(value)
            value
        }.getOrNull()
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
