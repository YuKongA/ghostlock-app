package com.ghostlock.app.data

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SilentKernelSuLauncherTest {
    @get:Rule
    val temporary = TemporaryFolder()

    @Test
    fun silentLauncherSuppressesManagerStartAndPreservesOtherCommands() {
        assumeTrue(File("/bin/sh").canExecute())
        val directory = temporary.newFolder("boot ksud's helpers")
        val systemAm = executable(temporary.newFile("system am"), """
            #!/bin/sh
            printf '<%s>\n' "${'$'}@"
        """.trimIndent())
        executable(
            File(directory, "am"),
            SilentKernelSuLauncher.amScript(systemAm.absolutePath).replaceFirst("#!/system/bin/sh", "#!/bin/sh"),
        )
        val ksud = executable(temporary.newFile("real ksud's binary"), """
            #!/bin/sh
            printf 'ksud:<%s>\n' "${'$'}@"
            am force-stop me.weishu.kernelsu
            am start -n me.weishu.kernelsu/me.weishu.kernelsu.ui.MainActivity
            am start -n me.weishu.kernelsu/.ui.MainActivity
            am start -n com.example/.OtherActivity
        """.trimIndent())
        val launcher = temporary.newFile("launcher").apply {
            writeText(SilentKernelSuLauncher.launcherScript(directory.absolutePath, ksud.absolutePath))
        }

        val process = ProcessBuilder("/bin/sh", launcher.absolutePath, "late-load", "--kmi", "android15-6.6")
            .redirectErrorStream(true)
            .start()
        val output = process.inputStream.bufferedReader().use { it.readLines() }
        assertEquals(0, process.waitFor())
        assertEquals(
            listOf(
                "ksud:<late-load>", "ksud:<--kmi>", "ksud:<android15-6.6>",
                "<force-stop>", "<me.weishu.kernelsu>",
                "<start>", "<-n>", "<com.example/.OtherActivity>",
            ),
            output,
        )
    }

    private fun executable(file: File, script: String): File = file.apply {
        writeText("$script\n")
        check(setExecutable(true))
    }
}
