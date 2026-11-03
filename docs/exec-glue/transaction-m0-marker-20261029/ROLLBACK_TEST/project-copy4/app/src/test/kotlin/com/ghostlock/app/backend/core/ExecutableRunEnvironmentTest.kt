package com.ghostlock.app.backend.core

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Verifies the exact 43499 launcher environment and keeps 43284's inherited environment unchanged. */
class ExecutableRunEnvironmentTest {
    @Test
    fun cve43499ReceivesOnlyTheThreeRunDirectoryPaths() {
        val directory = File("build/fixtures/43499-run").absoluteFile

        val variables = ExecutableRunEnvironment.variables("43499-Glued", directory)

        assertEquals(
            setOf("GHOSTLOCK_HOME", "TMPDIR", "GHOSTLOCK_KSU_LOG"),
            variables.keys,
        )
        assertEquals(directory.absolutePath, variables["GHOSTLOCK_HOME"])
        assertEquals(directory.absolutePath, variables["TMPDIR"])
        assertEquals(File(directory, ".ghostlock_ksu.log").absolutePath, variables["GHOSTLOCK_KSU_LOG"])
        assertTrue(variables["GHOSTLOCK_KSU_LOG"] != File(directory, "run.log").absolutePath)
    }

    @Test
    fun cve43284DoesNotReceive43499EnvironmentOverrides() {
        assertTrue(ExecutableRunEnvironment.variables("43284Glued", File("build/fixtures/43284-run")).isEmpty())
    }
}
