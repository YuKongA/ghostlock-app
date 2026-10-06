package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * S4 R4 pure validation rules for the advanced/execution editors.
 *
 * HOCON refactor (2026-10-05): the 43284 profile surface is the EXECUTION TUNING
 * only. carrier_path / lkm_path / kmi are native-side conventions (the wire keeps
 * them, a profile must not provide them, and ProfileLayout rejects them on
 * sight), so no string field is editable here any more. The handshake knobs are
 * native `uint32_t` (0..0xFFFFFFFF) and now live under
 * `backend.cve_2026_43284.execution.*`; every other leaf keeps the historical
 * "must parse as a Long" rule.
 */
class FieldValidationTest {
    private val section = Cve2026_43284Fields.Section
    private val wait = "$section.execution.wait_timeout_ms"
    private val pollAttempts = "$section.execution.module_poll_attempts"
    private val pollInterval = "$section.execution.module_poll_interval_ms"

    @Test
    fun `the editable 43284 surface is the execution tuning only`() {
        /* No string field: the conventions are native-side. */
        assertTrue(Cve2026_43284Fields.StringPaths.isEmpty())
        assertTrue(Cve2026_43284Fields.FilesystemPaths.isEmpty())
        assertEquals(
            listOf(
                "$section.execution.wait_timeout_ms",
                "$section.execution.module_poll_attempts",
                "$section.execution.module_poll_interval_ms",
            ),
            Cve2026_43284Fields.UInt32Paths,
        )
        assertEquals(Cve2026_43284Fields.UInt32Paths, Cve2026_43284Fields.EditablePaths)
    }

    @Test
    fun `uint32 handshake knobs reject the full out-of-range set`() {
        for (path in listOf(wait, pollAttempts, pollInterval)) {
            assertFalse(isFieldInputInvalid(path, "0"))
            assertFalse(isFieldInputInvalid(path, "15000"))
            assertFalse(isFieldInputInvalid(path, "4294967295"))
            assertTrue(isFieldInputInvalid(path, "-1"))
            assertTrue(isFieldInputInvalid(path, "4294967296"))
            assertTrue(isFieldInputInvalid(path, "99999999999999"))
            assertTrue(isFieldInputInvalid(path, "not-a-number"))
            assertTrue(isFieldInputInvalid(path, ""))
        }
    }

    @Test
    fun `non-43284 leaves keep the historical numeric rule`() {
        assertFalse(isFieldInputInvalid("offset.init_task", "-1"))
        assertFalse(isFieldInputInvalid("cred.ref0_image", "-274696158080"))
        assertTrue(isFieldInputInvalid("offset.init_task", "abc"))
        assertTrue(isFieldInputInvalid("offset.init_task", ""))
    }
}
