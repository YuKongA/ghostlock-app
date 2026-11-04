package com.ghostlock.app.backend.backends.cve43499.config

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * P1b-1 debug 配置单测（task-64）：G01 `failStop.enabled` 与 G08 `execution.*_cpu`
 * （A301SO/mondrian 两份 conf；缺省/非法 fail-closed；派生执行参数覆盖）。
 */
class FailStopCpuConfigTest {

    @Test
    fun `both debug profiles enable fail stop and no pin owner waiter`() {
        for (asset in listOf(A301SO_ASSET, MONDRIAN_ASSET)) {
            val debug = Cve43499DebugConfig.parse(readAsset(asset))
            assertEquals("$asset failStop.enabled", 1, debug.failStop.enabled)
            assertEquals("$asset owner", Cve43499DebugConfig.NO_PIN_CPU, debug.cpu.ownerCpu)
            assertEquals("$asset waiter", Cve43499DebugConfig.NO_PIN_CPU, debug.cpu.waiterCpu)
            assertEquals("$asset consumer", 2, debug.cpu.consumerCpu)
            val execution = Cve43499DebugConfig.executionParameters(debug)
            assertEquals("$asset execution owner", -1, execution.ownerCpu)
            assertEquals("$asset execution waiter", -1, execution.waiterCpu)
            assertEquals("$asset execution consumer", 2, execution.consumerCpu)
        }
    }

    @Test
    fun `g05 writeChain flags default false and parse fail closed`() {
        for (asset in listOf(A301SO_ASSET, MONDRIAN_ASSET)) {
            val debug = Cve43499DebugConfig.parse(readAsset(asset))
            assertTrue("$asset prebuild default false", !debug.fastRepair.fastRepairPrebuildEnabled)
            assertTrue("$asset allowFallback default false", !debug.fastRepair.allowFullFallback)
        }
        val base = readAsset(A301SO_ASSET)
            .replace("fast_repair_prebuild_enabled = 0", "fast_repair_prebuild_enabled = 1")
            .replace("allow_full_fallback = 0", "allow_full_fallback = 1")
        val enabled = Cve43499DebugConfig.parse(base)
        assertTrue(enabled.fastRepair.fastRepairPrebuildEnabled)
        assertTrue(enabled.fastRepair.allowFullFallback)
        expectFailure("debug-config-invalid:writeChain.flags") {
            Cve43499DebugConfig.parse(
                readAsset(A301SO_ASSET).replace(
                    "allow_full_fallback = 0",
                    "allow_full_fallback = 2",
                ),
            )
        }
    }

    @Test
    fun `missing sections keep legacy defaults`() {
        val text = readAsset(A301SO_ASSET)
            .replace(Regex("\\nfailStop \\{[^}]*\\}\\n"), "\n")
            .replace(Regex("\\nexecution \\{[^}]*\\}\\n"), "\n")
        val debug = Cve43499DebugConfig.parse(text)
        assertEquals(1, debug.failStop.enabled)
        assertEquals(0, debug.cpu.ownerCpu)
        assertEquals(1, debug.cpu.waiterCpu)
        assertEquals(2, debug.cpu.consumerCpu)
    }

    @Test
    fun `invalid fail stop or cpu values fail closed`() {
        val base = readAsset(A301SO_ASSET)
        expectFailure("debug-config-invalid:failStop.enabled") {
            Cve43499DebugConfig.parse(base.replace("enabled = 1\n}", "enabled = 2\n}"))
        }
        expectFailure("debug-config-invalid:execution.cpu") {
            Cve43499DebugConfig.parse(base.replace("owner_cpu = -1", "owner_cpu = -2"))
        }
        expectFailure("debug-config-invalid:execution.cpu") {
            Cve43499DebugConfig.parse(base.replace("consumer_cpu = 2", "consumer_cpu = -1"))
        }
    }

    /** @param prefix 期望异常前缀。 @param block 触发块。 */
    private fun expectFailure(prefix: String, block: () -> Unit) {
        try {
            block()
            throw AssertionError("expected failure: $prefix")
        } catch (expected: Cve43499DebugConfigException) {
            assertTrue(
                "unexpected reason: ${expected.message}",
                expected.message?.startsWith(prefix) == true,
            )
        }
    }

    /** @param asset 相对 debug assets 的路径。 @return 文本。 */
    private fun readAsset(asset: String): String {
        val relative = "app/src/debug/assets/$asset"
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        val file = candidates.firstOrNull { it.isFile }
            ?: error("debug asset not found: $asset from ${File(".").absolutePath}")
        return file.readText()
    }

    private companion object {
        const val A301SO_ASSET: String = "profile/43499/debug-a301so.conf"
        const val MONDRIAN_ASSET: String = "profile/43499/debug-mondrian.conf"
    }
}
