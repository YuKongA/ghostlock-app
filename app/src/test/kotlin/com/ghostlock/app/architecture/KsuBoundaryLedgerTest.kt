package com.ghostlock.app.architecture

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Separates removable KernelSU manager residue from real exploit and handoff contracts. */
class KsuBoundaryLedgerTest {
    /** Verifies the 69 Kotlin plus one AIDL delete closure and the exact KEEP evidence set. */
    @Test
    fun `ksu remove and keep ledgers remain classified`() {
        val result = D0ToolTestSupport.verify("ksu")
        assertEquals(result.output, 0, result.exitCode)
        assertTrue(result.output, result.output.contains("REMOVE_RESIDUE=0"))
        assertTrue(result.output, result.output.contains("KEEP_DRIFT=0"))
    }
}
