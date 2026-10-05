package com.ghostlock.app.data

import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * F4 no-hardcode guard: combination tokens may live only in the exported
 * manifest resource (tests and assets are allowed). The token list is read from
 * the loaded catalogue, so extending the native table automatically extends the
 * check; runtime Kotlin must resolve every token through [CombinationCatalog].
 *
 * Two pre-existing literals coincide with the token "shizuku" but are not
 * selection tokens (a debug-log entry label that names the archived file and a
 * Compose list item key). They are exempted per occurrence — file, literal and
 * the exact line fragment — and the exemption must be used, so a stale entry
 * fails the test just like the R1 include firewall whitelist.
 */
class CombinationTokenHardcodeTest {

    private data class ExemptLiteral(
        val file: String,
        val literal: String,
        val lineFragment: String,
        val reason: String,
    )

    private val exemptions = listOf(
        ExemptLiteral(
            file = "app/src/main/kotlin/com/ghostlock/app/data/AndroidGhostlockRepository.kt",
            literal = "shizuku",
            lineFragment = "withDebugAttackLog(\"shizuku\"",
            reason = "debug-log entry label (archives ghostlock-shizuku-*.log), not a selection token",
        ),
        ExemptLiteral(
            file = "app/src/main/kotlin/com/ghostlock/app/ui/AboutUI.kt",
            literal = "shizuku",
            lineFragment = "item(key = \"shizuku\")",
            reason = "Compose list item key, not a selection token",
        ),
    )

    @Test
    fun runtimeKotlinContainsNoCombinationTokenLiteral() {
        val tokens = CombinationCatalog.specs.map { it.token }
        assertTrue("combination catalogue is empty", tokens.isNotEmpty())
        val root = repoRoot()
        val used = BooleanArray(exemptions.size)
        val offenders = mutableListOf<String>()
        for (relative in listOf("profile-core/src/main/kotlin", "app/src/main/kotlin")) {
            val dir = File(root, relative)
            assertTrue("missing runtime source directory: " + relative, dir.isDirectory)
            dir.walkTopDown().filter { it.isFile && it.extension == "kt" }.forEach { file ->
                val path = file.relativeTo(root).path
                file.readLines().forEachIndexed { index, line ->
                    for (token in tokens) {
                        if (!line.contains("\"" + token + "\"")) continue
                        val exempt = exemptions.indexOfFirst {
                            it.file == path && it.literal == token && line.contains(it.lineFragment)
                        }
                        if (exempt >= 0) {
                            used[exempt] = true
                        } else {
                            offenders += path + ":" + (index + 1) + ": \"" + token + "\""
                        }
                    }
                }
            }
        }
        val stale = exemptions.filterIndexed { index, _ -> !used[index] }.map { it.file + ": " + it.reason }
        assertTrue("stale hardcode exemption (literal gone; remove it): " + stale, stale.isEmpty())
        assertTrue(
            "combination tokens must be read from the manifest resource, not hard-coded: " + offenders,
            offenders.isEmpty(),
        )
    }

    private fun repoRoot(): File {
        var current = File(requireNotNull(System.getProperty("user.dir"))).canonicalFile
        repeat(5) {
            if (File(current, "app/src/main/kotlin/com/ghostlock/app").isDirectory) return current
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
