package com.ghostlock.app

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 攻击流程的静态守卫：固定 marker 与信号文件名不得回到 `app/src/main/kotlin`。
 *
 * 只扫字符串，不判断语义；命中即失败，防止后续批次把旧协议又接回来。
 */
class AttackFlowGuardTest {

    private val forbidden = listOf(
        "ghostlock_lkm_ok",
        "ghostlock_lkm_fail",
        "dfm0",
        "dfm1",
        "ghostlock-lkm-ready",
        "ghostlock-lkm-connected",
        "attack.pid",
        "finish/runs",
        "pending.sh",
        "finish.params",
        "root_cmd.sh",
    )

    @Test
    fun attackFlowHasNoSignalMarkers() {
        val hits = scan(sourceRoot(), forbidden)
        assertTrue("攻击流程出现旧 marker/路径：" + hits.joinToString(), hits.isEmpty())
    }

    /** 攻击链路里允许写盘的只有脚本、日志与启动计数；其余写 API 一律失败。 */
    private val writeApis = listOf(
        "writeText(", "writeBytes(", "FileOutputStream(", "outputStream(", "copyTo(",
    )
    private val writeWhitelist = setOf("FinishScriptStore.kt", "LogMemfd.kt", "BootSettings.kt")
    private val writeScope = listOf(
        "com/ghostlock/app/attack",
        "com/ghostlock/app/engine",
        "com/ghostlock/app/jni",
        "com/ghostlock/app/core/access",
        "com/ghostlock/app/ui/screen/module/backends",
    )

    @Test
    fun writesOnlyInWhitelistedFiles() {
        val hits = scanWrites(sourceRoot())
        assertTrue("攻击链路出现白名单外的写盘调用：" + hits.joinToString(), hits.isEmpty())
    }

    @Test
    fun scannerCanFail() {
        val dir = Files.createTempDirectory("guard").toFile()
        dir.resolve("Sample.kt").writeText("val marker = \"ghostlock_lkm_ok\"\n")
        assertTrue(scan(dir, forbidden).isNotEmpty())
    }

    @Test
    fun writeScannerCanFail() {
        val dir = Files.createTempDirectory("guard-write").toFile()
        val file = dir.resolve("com/ghostlock/app/attack/Bad.kt")
        file.parentFile.mkdirs()
        file.writeText("val bad = \"x\".writeText(\"y\")\n")
        assertTrue(scanWrites(dir).isNotEmpty())
    }

    private fun sourceRoot(): File {
        val cwd = File(System.getProperty("user.dir"))
        val candidates = listOf(
            cwd.resolve("app/src/main/kotlin"),
            cwd.resolve("src/main/kotlin"),
            cwd.resolve("../app/src/main/kotlin"),
        )
        return candidates.firstOrNull { it.isDirectory }
            ?: error("cannot locate app/src/main/kotlin from " + cwd)
    }

    private fun scan(root: File, needles: List<String>): List<String> =
        root.walkTopDown()
            .filter { it.isFile && it.extension == "kt" }
            .flatMap { file ->
                file.readLines().mapIndexedNotNull { index, line ->
                    needles.firstOrNull { needle -> line.contains(needle) }
                        ?.let { needle -> file.name + ":" + (index + 1) + ":" + needle }
                }.asSequence()
            }
            .toList()

    private fun scanWrites(root: File = sourceRoot()): List<String> =
        writeScope.asSequence()
            .map { root.resolve(it) }
            .filter { it.isDirectory }
            .flatMap { it.walkTopDown() }
            .filter { it.isFile && it.extension == "kt" && it.name !in writeWhitelist }
            .flatMap { file ->
                file.readLines().mapIndexedNotNull { index, line ->
                    writeApis.firstOrNull { api -> line.contains(api) }
                        ?.let { api -> file.name + ":" + (index + 1) + ":" + api }
                }.asSequence()
            }
            .toList()
}
