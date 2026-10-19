package com.ghostlock.app.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EngineLogTest {
    @Test
    fun infoHasNoParameters() {
        val line = EngineLog.info("Page cache engine started")
        assertFalse(line.contains('\t'))
        assertFalse(line.contains('='))
    }

    @Test
    fun warnCarriesExactlyOneReason() {
        val line = EngineLog.warn("Page cache step degraded", "hook-write-refused")
        assertEquals("Page cache step degraded\thook-write-refused", line)
        assertEquals(1, line.count { it == '\t' })
    }

    @Test
    fun errorCarriesExactlyOneReason() {
        val line = EngineLog.error("Page cache step failed", "module-missing")
        assertEquals("Page cache step failed\tmodule-missing", line)
    }

    @Test
    fun debugSeparatesParametersWithTabs() {
        val line = EngineLog.debug("Page cache write", "offset=0x10", "bytes=16")
        assertEquals("Page cache write\toffset=0x10\tbytes=16", line)
        assertTrue(line.contains('\t'))
    }

    @Test(expected = IllegalArgumentException::class)
    fun infoRejectsParameters() {
        EngineLog.info("Page cache engine started\tbackend=43284")
    }
}
