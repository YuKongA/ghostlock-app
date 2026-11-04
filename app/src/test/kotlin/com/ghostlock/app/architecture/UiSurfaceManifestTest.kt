package com.ghostlock.app.architecture

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Locks the dual-skin production surface matrix and the deferred Run-state boundary. */
class UiSurfaceManifestTest {
    /** Verifies 88 rows, 68 D0 captures, 20 deferred-D5 rows, and 132 D0 records. */
    @Test
    fun `surface matrix has the approved split`() {
        val result = D0ToolTestSupport.verify("surface")
        assertEquals(result.output, 0, result.exitCode)
        assertTrue(result.output, result.output.contains("SURFACES=88"))
        assertTrue(result.output, result.output.contains("D0_CAPTURE=68"))
        assertTrue(result.output, result.output.contains("DEFERRED_D5=20"))
        assertTrue(result.output, result.output.contains("SEMANTICS_RECORDS=132"))
    }
}
