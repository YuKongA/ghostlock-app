package com.ghostlock.app.engine.pagecache

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test

/** 用模拟器 libc++ 的本地副本验证 Kotlin 解析/派生（文件不在时跳过）。 */
class KotlinElfPlannerEmulatorTest {

    @Test
    fun derivesPlanFromEmulatorLibcxx() {
        val file = FixtureFiles.emulibcxx()
        val target = ElfTarget(file.absolutePath, file.readBytes())
        val layout = KotlinElfPlanner.parse(target.bytes)
        assertNotNull("layout is null", layout)
        val symbol = KotlinElfPlanner.findSymbol(
            target.bytes,
            layout!!,
            "_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_",
        )
        assertNotNull("symbol is null", symbol)
        val payload = ByteArray(480) { 0x11 }
        val plan = KotlinElfPlanner.plan(target, payload)
        assertNotNull("plan is null", plan)
        plan!!
        assertEquals(0x6EB54L, plan.siteFile)
        assertEquals(0x9A160L, plan.landingFile)
        assertTrue("payload max " + plan.payloadMax, plan.payloadMax >= payload.size)
        assertTrue(plan.branchWord != 0)
        assertTrue(plan.jumpBackWord != 0)
    }
}
