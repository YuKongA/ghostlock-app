package com.ghostlock.app.architecture

import java.nio.file.Files
import java.nio.file.Path
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Runs the current file-action and forbidden-edge inventory through its single host tool.
 *
 * @note Ordinary source identity is tracked by PRESENT/ABSENT state rather than repeated SHA calculation.
 * A non-zero tool exit is a hard failure and its complete merged output is included in the assertion.
 */
class ArchitectureInventoryTest {
    /** Verifies that every current production Kotlin file has one explicit action row. */
    @Test
    fun `production Kotlin inventory has no unclassified file`() {
        val result = D0ToolTestSupport.verify("inventory")
        assertEquals(result.output, 0, result.exitCode)
        assertTrue(result.output, result.output.contains("D0_VERIFY=PASS"))
        assertTrue(result.output, result.output.contains("UNCLASSIFIED=0"))

        val architecture = D0ToolTestSupport.verify("architecture")
        assertEquals(architecture.output, 0, architecture.exitCode)
        assertTrue(architecture.output, architecture.output.contains("D0_VERIFY=PASS"))
        assertTrue(architecture.output, architecture.output.contains("ARCH_VIOLATIONS=0"))
    }
}

/** Host-tool result shared by the four D0 architecture tests. */
internal data class D0ToolResult(val exitCode: Int, val output: String)

/** Resolves and invokes the repository-owned D0 tool without duplicating its scanner in tests. */
internal object D0ToolTestSupport {
    /**
     * Verifies one manifest section.
     *
     * @param section architecture, inventory, component, ksu, or surface.
     * @return process exit and merged output.
     * @note Python and the repository root are required host-test prerequisites; no files are written.
     */
    fun verify(section: String): D0ToolResult {
        val repo = locateRepository()
        val command = listOf(
            "python3",
            repo.resolve("tools/kotlin_architecture_d0.py").toString(),
            "verify",
            "--repo",
            repo.toString(),
            "--fixtures",
            repo.resolve("app/src/test/resources/architecture").toString(),
            "--section",
            section,
        )
        val process = ProcessBuilder(command)
            .directory(repo.toFile())
            .redirectErrorStream(true)
            .start()
        val output = process.inputStream.bufferedReader().use { it.readText() }
        return D0ToolResult(process.waitFor(), output)
    }

    /** @return nearest ancestor containing the D0 tool; throws when the checkout is incomplete. */
    private fun locateRepository(): Path {
        var current = Path.of(System.getProperty("user.dir")).toAbsolutePath().normalize()
        repeat(8) {
            if (Files.isRegularFile(current.resolve("tools/kotlin_architecture_d0.py"))) {
                return current
            }
            current = current.parent ?: error("repository root not found")
        }
        error("repository root not found")
    }
}
