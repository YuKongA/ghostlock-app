package com.ghostlock.app.core.runhost

import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pure JVM contract cases for stable RunHost codes and bounded grammar. */
class RunHostProtocolTest {
    /** Stable integer values remain independent of enum ordinal. */
    @Test
    fun stableCodesRemainPinned() {
        assertEquals(0, RunHostCode.OK.wireValue)
        assertEquals(1, RunHostCode.ALREADY_APPLIED.wireValue)
        assertEquals(10, RunHostCode.BAD_STATE.wireValue)
        assertEquals(24, RunHostCode.EXECUTION_DOMAIN_MISMATCH.wireValue)
        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, RunHostCode.fromWireValue(999))
    }

    /** Every wire identity grammar accepts only its canonical bounded form. */
    @Test
    fun wireGrammarIsStrict() {
        assertTrue(RunHostProtocol.isRunId("0".repeat(32)))
        assertFalse(RunHostProtocol.isRunId("0".repeat(31)))
        assertTrue(RunHostProtocol.isBackendKey("DIAGNOSE"))
        assertFalse(RunHostProtocol.isBackendKey("bad key"))
        assertTrue(RunHostProtocol.isLogicalId("profile-v3"))
        assertFalse(RunHostProtocol.isLogicalId("Profile"))
        assertTrue(RunHostProtocol.isSha256("a".repeat(64)))
        assertFalse(RunHostProtocol.isSha256("A".repeat(64)))
    }

    /** Stable log wire values remain independent of enum ordinal. */
    @Test
    fun logWireValuesRemainPinned() {
        assertEquals(1, LogLevel.Verbose.wireValue)
        assertEquals(5, LogLevel.Error.wireValue)
        assertEquals(1, LogOrigin.Kotlin.wireValue)
        assertEquals(3, LogOrigin.Shell.wireValue)
    }

}
