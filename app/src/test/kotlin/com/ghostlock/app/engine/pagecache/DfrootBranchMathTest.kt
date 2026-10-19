package com.ghostlock.app.engine.pagecache

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assume
import org.junit.Test

class DfrootBranchMathTest {
    @Test
    fun branchAndJumpMatchDfrootFormula() {
        val file = FixtureFiles.emulibcxx()
        val target = ElfTarget(file.absolutePath, file.readBytes())
        val payload = DfrootLibcxx.bytes()
        val plan = KotlinElfPlanner.plan(target, payload)
        assertEquals(0x1400ADA7, plan!!.branchWord)
        val jump = KotlinElfPlanner.encodeBranch(
            plan.landingVaddr + DfrootLibcxx.JUMP_BACK_OFFSET,
            plan.siteVaddr + 4,
        )
        assertEquals(0x17FF520B, jump)
    }
}
