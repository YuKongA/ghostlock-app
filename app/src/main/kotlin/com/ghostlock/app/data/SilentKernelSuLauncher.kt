package com.ghostlock.app.data

import java.io.File

internal object SilentKernelSuLauncher {
    fun prepare(workDir: File, ksud: File): File {
        val directory = File(workDir, ".ghostlock-boot-ksud")
        check(directory.isDirectory || directory.mkdir()) { "Cannot create ${directory.absolutePath}" }
        writeExecutable(File(directory, "am"), amScript("/system/bin/am"))
        return writeExecutable(
            File(directory, "ksud"),
            launcherScript(directory.absolutePath, ksud.absolutePath),
        )
    }

    internal fun launcherScript(amDirectory: String, ksud: String): String = """
        #!/system/bin/sh
        PATH=${quote(amDirectory)}:"${'$'}PATH" exec ${quote(ksud)} "${'$'}@"
    """.trimIndent() + "\n"

    internal fun amScript(systemAm: String): String = """
        #!/system/bin/sh
        if [ "${'$'}#" -eq 3 ] && [ "${'$'}1" = start ] && [ "${'$'}2" = -n ]; then
            case "${'$'}3" in
                */me.weishu.kernelsu.ui.MainActivity|me.weishu.kernelsu/.ui.MainActivity|me.weishu.kernelsu.pr/.ui.MainActivity)
                    if [ -x /system/bin/log ]; then
                        /system/bin/log -p i -t GhostLockBoot "Suppressed KernelSU Manager launch"
                    fi
                    exit 0
                    ;;
            esac
        fi
        exec ${quote(systemAm)} "${'$'}@"
    """.trimIndent() + "\n"

    private fun writeExecutable(file: File, script: String): File {
        file.writeText(script)
        check(file.setExecutable(true, true)) { "Cannot make ${file.absolutePath} executable" }
        return file
    }

    private fun quote(value: String): String = "'" + value.replace("'", "'\\''") + "'"
}
