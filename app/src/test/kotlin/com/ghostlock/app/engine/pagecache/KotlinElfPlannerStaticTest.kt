package com.ghostlock.app.engine.pagecache

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test

/** 静态 crash_dump64 入口 plan（文件不在时跳过）。 */
class KotlinElfPlannerStaticTest {
    @Test
    fun derivesEntryPlanFromCrashDump64() {
        val file = FixtureFiles.crashDump64()
        val target = ElfTarget(file.absolutePath, file.readBytes())
        val plan = KotlinElfPlanner.planEntry(target, ByteArray(480) { 0x22 })
        assertNotNull("plan is null", plan)
        plan!!
        assertEquals(0x428L, plan.siteFile)
        assertEquals(0x430L, plan.landingFile)
        assertTrue(plan.payloadMax >= 480)
        assertTrue(plan.branchWord != 0)
    }
}
