package com.ghostlock.app.backend.backends.cve43499

import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * B7 KernelSnitch scan bounds fail-closed 契约（task-81）——**先写测试、不运行**。
 *
 * host-parseable：detector 常量单源派生头、placement 严格相等校验、A301SO/
 * mondrian executionParameters 的 scan/identity 值一致；不加载 .so、不编译。
 */
class KernelsnitchBoundsContractTest {

    @Test
    fun `detector bounds header is the single authority and placement compares all fields`() {
        val detector = readRepoFile("app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h")
        val bounds = readRepoFile("app/src/main/cpp/backends/43499/core/kernelsnitch_bounds_internal.h")
        val placement = readRepoFile("app/src/main/cpp/backends/43499/core/placement.c")

        /* detector 常量来源未改；bounds 头只派生。 */
        assertTrue(detector.contains("#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL"))
        assertTrue(detector.contains("#define KERNELSNITCH_IDENTITY_END (KERNELSNITCH_IDENTITY_START + (64ULL<<30))"))
        assertTrue(detector.contains("#define IDENTITY_START KERNELSNITCH_IDENTITY_START"))
        assertTrue(detector.contains("#define IDENTITY_END   KERNELSNITCH_IDENTITY_END"))
        assertTrue(detector.contains("#define COARSE_SZ (1ULL << 30)"))
        assertTrue(bounds.contains("#include \"kernelsnitch/kernelsnitch.h\""))
        assertTrue(bounds.contains("GLK43499_KERNELSNITCH_IDENTITY_START"))
        assertTrue(bounds.contains("GLK43499_KERNELSNITCH_IDENTITY_END"))
        assertTrue(bounds.contains("GLK43499_KERNELSNITCH_SCAN_LOWER_BOUND"))
        assertTrue(bounds.contains("GLK43499_KERNELSNITCH_SCAN_UPPER_BOUND"))
        assertTrue(bounds.contains("GLK43499_KERNELSNITCH_SCAN_STEP"))
        assertTrue(bounds.contains("scan-bounds-mismatch"))

        /* placement 用头常量严格比较五个字段，并映射到具名 fail-closed 分支。 */
        assertTrue(placement.contains("kernelsnitch_bounds_internal.h"))
        assertTrue(placement.contains("request->identity_start != "))
        assertTrue(placement.contains("request->identity_end != "))
        assertTrue(placement.contains("request->scan_lower_bound != "))
        assertTrue(placement.contains("request->scan_upper_bound != "))
        assertTrue(placement.contains("request->scan_step != "))
        assertTrue(placement.contains("GLK43499_PLACEMENT_VALIDATE_SCAN_BOUNDS_MISMATCH"))
        assertTrue(placement.contains("validation == GLK43499_PLACEMENT_VALIDATE_SCAN_BOUNDS_MISMATCH"))
    }

    @Test
    fun `expected detector range is arm39 identity start plus 64GiB`() {
        assertEquals(0xffffff8000000000uL, EXPECTED_START)
        assertEquals(0xffffff9000000000uL, EXPECTED_END)
        assertEquals(0x1000uL, EXPECTED_SCAN_STEP)
        assertEquals(EXPECTED_START, EXPECTED_END - (64uL shl 30))
    }

    @Test
    fun `a301so and mondrian execution parameters match detector bounds`() {
        for (asset in listOf(A301SO_ASSET, MONDRIAN_ASSET)) {
            val debug = A301SoDebugConfig.parse(readAsset(asset))
            val execution = A301SoDebugConfig.executionParameters(debug)
            assertEquals("$asset scanLowerBound", EXPECTED_START.toLong(),
                execution.scanLowerBound)
            assertEquals("$asset scanUpperBound", EXPECTED_END.toLong(),
                execution.scanUpperBound)
            assertEquals("$asset scanStep", EXPECTED_SCAN_STEP.toLong(),
                execution.scanStep)
            assertEquals("$asset identityStart", EXPECTED_START.toLong(),
                execution.identityStart)
            assertEquals("$asset identityEnd", EXPECTED_END.toLong(),
                execution.identityEnd)
            assertTrue(consistent(
                identityStart = execution.identityStart,
                identityEnd = execution.identityEnd,
                scanLower = execution.scanLowerBound,
                scanUpper = execution.scanUpperBound,
                scanStep = execution.scanStep,
            ))
        }
    }

    @Test
    fun `narrower wider and step mismatch are rejected by the B7 predicate`() {
        assertFalse(consistent(
            identityStart = EXPECTED_START.toLong() + 0x1000L,
            identityEnd = EXPECTED_END.toLong(),
            scanLower = EXPECTED_START.toLong(),
            scanUpper = EXPECTED_END.toLong(),
            scanStep = EXPECTED_SCAN_STEP.toLong(),
        ))
        assertFalse(consistent(
            identityStart = EXPECTED_START.toLong(),
            identityEnd = EXPECTED_END.toLong(),
            scanLower = EXPECTED_START.toLong(),
            scanUpper = EXPECTED_END.toLong() + 0x1000L,
            scanStep = EXPECTED_SCAN_STEP.toLong(),
        ))
        assertFalse(consistent(
            identityStart = EXPECTED_START.toLong(),
            identityEnd = EXPECTED_END.toLong(),
            scanLower = EXPECTED_START.toLong(),
            scanUpper = EXPECTED_END.toLong(),
            scanStep = 0x2000L,
        ))
        assertTrue(consistent(
            identityStart = EXPECTED_START.toLong(),
            identityEnd = EXPECTED_END.toLong(),
            scanLower = EXPECTED_START.toLong(),
            scanUpper = EXPECTED_END.toLong(),
            scanStep = EXPECTED_SCAN_STEP.toLong(),
        ))
    }

    /** @return true 当且仅当五字段与 detector 编译期常量严格相等（B7 判据镜像）。 */
    private fun consistent(
        identityStart: Long,
        identityEnd: Long,
        scanLower: Long,
        scanUpper: Long,
        scanStep: Long,
    ): Boolean = identityStart == EXPECTED_START.toLong() &&
        identityEnd == EXPECTED_END.toLong() &&
        scanLower == EXPECTED_START.toLong() &&
        scanUpper == EXPECTED_END.toLong() &&
        scanStep == EXPECTED_SCAN_STEP.toLong()

    /** @param relative 相对仓库路径。 @return 文本。 */
    private fun readRepoFile(relative: String): String {
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return (candidates.firstOrNull { it.isFile } ?: error("missing repo file: $relative"))
            .readText()
    }

    /** @param asset 相对 debug assets。 @return 文本。 */
    private fun readAsset(asset: String): String {
        val relative = "app/src/debug/assets/$asset"
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        return (candidates.firstOrNull { it.isFile } ?: error("missing asset: $asset")).readText()
    }

    private companion object {
        val EXPECTED_START: ULong = 0xffffff8000000000uL
        val EXPECTED_END: ULong = 0xffffff9000000000uL
        val EXPECTED_SCAN_STEP: ULong = 0x1000uL
        const val A301SO_ASSET: String = "profile/43499/debug-a301so.conf"
        const val MONDRIAN_ASSET: String = "profile/43499/debug-mondrian.conf"
    }
}
