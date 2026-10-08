package com.ghostlock.app.data.profile

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.mutableChild
import com.ghostlock.app.data.valueMapOf
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.asValueList
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * Design 2.9-1 (user ruling): the exporter validates the must-have parameters of
 * the paths a profile DECLARES usable, not of every path in the catalogue.
 *
 * The fixtures are discovered the way the exporter discovers them - via
 * index.conf - and classified by their PARSED available declaration, so a future
 * asset rename or a profile that starts declaring 43499 does not silently
 * invalidate the guard (no file-name or comment-text coupling).
 */
class GeneralProfileValidationTest {

    private val repo: File = repoRoot()
    private val profiles: File = File(repo, "app/src/main/assets/profile")

    private fun repoRoot(): File {
        var current = File(".").absoluteFile
        while (current.parentFile != null) {
            if (File(current, "app/src/main/assets/profile").isDirectory) return current
            current = current.parentFile
        }
        error("cannot locate the repository root from " + File(".").absolutePath)
    }

    private fun inner(text: String): ValueMap {
        val parsed = requireNotNull(HoconSupport.parseValue(text).asValueMap())
        return parsed["ghostlock"].asValueMap() ?: parsed
    }

    private fun innerOf(file: File): ValueMap = inner(file.readText())

    private fun declaredBackends(file: File): Set<String>? =
        innerOf(file)["available"].asValueMap()?.keys?.filterIsInstance<String>()?.toSet()

    /** The indexed profiles, exactly as the exporter enumerates them. */
    private fun indexedProfiles(): List<File> {
        val index = requireNotNull(HoconSupport.parseValue(File(profiles, "index.conf").readText()).asValueMap())
        return requireNotNull(index["profiles"].asValueList()).mapNotNull { entry ->
            (entry.asValueMap()?.get("file") as? String)?.let { File(profiles, it) }
        }
    }

    private fun only43284Profiles(): List<File> =
        indexedProfiles().filter { it.isFile && declaredBackends(it) == setOf("cve_2026_43284") }

    private fun copyAssets(): File {
        val target = Files.createTempDirectory("general-profiles").toFile()
        profiles.copyRecursively(target, overwrite = true)
        return target
    }

    /** The exporter refuses an output dir outside the BUILD DIR it is given. */
    private fun outputDir(name: String): File =
        File(repo, "build/tmp/general-validation/" + name + "-" + System.nanoTime())

    /** The build tree the Gradle task hands to the exporter (may be a symlink). */
    private val buildDir: File get() = File(repo, "build").canonicalFile

    @Test
    fun `every 43284-only indexed profile validates without a route`() {
        val only = only43284Profiles()
        assertTrue(
            "at least one indexed profile must declare 43284 only (the general set)",
            only.isNotEmpty(),
        )
        for (file in only) {
            val runtime = ValueMap().also { it.putAll(innerOf(file)) }
            ProfileLayout.applyNormalize(runtime)
            val errors = ProfileResolver.validateMerged(runtime, null)
            assertEquals(file.name + " must validate without 43499: " + errors, emptyList<Any>(), errors)
        }
    }

    @Test
    fun `the unmodified asset tree exports`() {
        val src = copyAssets()
        val out = outputDir("clean")
        ProfileExporter.main(
            arrayOf(
                src.absolutePath,
                out.absolutePath,
                out.absolutePath,
                buildDir.absolutePath,
            ),
        )
        assertTrue("the export must produce documents", (out.listFiles()?.size ?: 0) > 0)
    }

    @Test
    fun `declaring 43499 again without its ABI is refused by the shared criterion`() {
        /* FALSIFICATION of the criterion: the same profile that validates as a
         * 43284-only declaration must FAIL once it declares 43499 usable, whether
         * it carries no ABI at all (section paths) or the null skeleton the 5.15
         * general inlines (field paths). Driven through ProfileResolver directly so
         * the assertion does not depend on which sections an asset happens to
         * carry; the exporter calls exactly this function. */
        val victim = only43284Profiles().first()
        val declared = ValueMap().also { it.putAll(innerOf(victim)) }
        ProfileLayout.applyNormalize(declared)
        assertEquals(
            "the fixture must start 43284-only",
            emptyList<String>(),
            ProfileResolver.validateMerged(declared, null).map { it.fieldPath },
        )
        declared.mutableChild("available")["cve_2026_43499"] =
            valueMapOf("route" to "multicast_waiter")
        val paths = ProfileResolver.validateMerged(declared, "multicast_waiter")
            .map { it.fieldPath }
        val reArmed = listOf("route", "task_struct", "cred", "offset").filter { section ->
            paths.any { it == section || it.startsWith(section + ".") }
        }
        assertTrue(
            "declaring 43499 must re-require its parameters, got: " + paths,
            reArmed.isNotEmpty(),
        )
    }

}
