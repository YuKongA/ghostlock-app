package com.ghostlock.app.backend.transport

import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.StageState
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Host-JVM process carrier observations without product-success inference. */
@RunWith(RobolectricTestRunner::class)
class BackendProcessCarrierTest {
    /** Exit zero and bounded stdout become a stage fact plus diagnostics. */
    @Test
    fun exitZeroIsOnlyAStageSuccess() {
        val sourceShell = File("/bin/sh")
        assumeTrue(sourceShell.isFile)
        val directory = Files.createTempDirectory("ghostlock-carrier-test").toFile()
        try {
            val shell = stageShell(sourceShell, directory)
            val result = BackendProcessCarrier().run(
                BackendProcessRequest(
                    executable = shell,
                    executableIdentity = PublishedArtifactIdentity.capture(shell),
                    arguments = listOf("-c", "printf carrier-ok"),
                    workingDirectory = directory,
                ),
                RunCancellationView { false },
            )
            assertEquals(StageState.SUCCESS, result.stage.state)
            assertEquals("carrier-ok", result.stdout)
        } finally {
            directory.deleteRecursively()
        }
    }

    /** Non-zero exit stays a failed backend stage regardless of diagnostic text. */
    @Test
    fun nonZeroExitCannotBecomeSuccessFromOutput() {
        val sourceShell = File("/bin/sh")
        assumeTrue(sourceShell.isFile)
        val directory = Files.createTempDirectory("ghostlock-carrier-test").toFile()
        try {
            val shell = stageShell(sourceShell, directory)
            val result = BackendProcessCarrier().run(
                BackendProcessRequest(
                    executable = shell,
                    executableIdentity = PublishedArtifactIdentity.capture(shell),
                    arguments = listOf("-c", "printf success; exit 7"),
                    workingDirectory = directory,
                ),
                RunCancellationView { false },
            )
            assertEquals(StageState.FAILED, result.stage.state)
            assertTrue(result.stdout.contains("success"))
        } finally {
            directory.deleteRecursively()
        }
    }

    /** Writes a private host wrapper because copied macOS platform binaries are killed by code-signing policy. */
    private fun stageShell(source: File, directory: File): File = File(directory, "carrier-shell").apply {
        writeText("#!${source.absolutePath}\nexec ${source.absolutePath} \"\$@\"\n")
        check(setExecutable(true, true))
    }
}
