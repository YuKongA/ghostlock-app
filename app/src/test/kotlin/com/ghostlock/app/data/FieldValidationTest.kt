package com.ghostlock.app.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * S4 R4 pure validation rules for the advanced/execution editors.
 *
 * The cve_2026_43284 text fields are wire type `str`: empty means absent,
 * otherwise the value must be within the native 256-byte UTF-8 bound, and the two
 * filesystem paths (carrier_path / lkm_path) must be absolute. The handshake
 * knobs are native `uint32_t`
 * (0..0xFFFFFFFF). Every other leaf keeps the historical "must parse as a Long"
 * rule.
 */
class FieldValidationTest {
    private val carrier = Cve2026_43284Fields.Section + ".carrier_path"
    private val lkm = Cve2026_43284Fields.Section + ".lkm_path"
    private val wait = Cve2026_43284Fields.Section + ".wait_timeout_ms"
    private val pollAttempts = Cve2026_43284Fields.Section + ".module_poll_attempts"
    private val pollInterval = Cve2026_43284Fields.Section + ".module_poll_interval_ms"

    @Test
    fun `policy paths accept empty and absolute paths`() {
        assertFalse(isFieldInputInvalid(carrier, ""))
        assertFalse(isFieldInputInvalid(lkm, "  "))
        assertFalse(isFieldInputInvalid(carrier, "/data/local/tmp/helper_custom.ko"))
    }

    @Test
    fun `policy paths reject relative paths`() {
        assertTrue(isFieldInputInvalid(carrier, "vendor/lib64/libbinderdebug.so"))
        assertTrue(isFieldInputInvalid(lkm, "helper.ko"))
    }

    @Test
    fun `policy paths are bounded by 256 UTF-8 bytes`() {
        val maxAscii = "/" + "a".repeat(Cve2026_43284Fields.MaxTextBytes - 1)
        assertFalse(isFieldInputInvalid(lkm, maxAscii))
        val tooLongAscii = "/" + "a".repeat(Cve2026_43284Fields.MaxTextBytes)
        assertTrue(isFieldInputInvalid(lkm, tooLongAscii))

        /* Multi-byte characters count by their UTF-8 byte length, not chars. */
        val maxMultibyte = "/" + "é".repeat(127) // 1 + 127*2 = 255 bytes
        assertFalse(isFieldInputInvalid(carrier, maxMultibyte))
        val tooLongMultibyte = "/" + "é".repeat(128) // 1 + 128*2 = 257 bytes
        assertTrue(isFieldInputInvalid(carrier, tooLongMultibyte))
    }

    @Test
    fun `uint32 handshake knobs reject the full out-of-range set`() {
        for (path in listOf(wait, pollAttempts, pollInterval)) {
            assertFalse(isFieldInputInvalid(path, "0"))
            assertFalse(isFieldInputInvalid(path, "15000"))
            assertFalse(isFieldInputInvalid(path, "4294967295")) // 0xFFFFFFFF
            assertTrue(isFieldInputInvalid(path, "-1"))
            assertTrue(isFieldInputInvalid(path, "4294967296")) // 0x1_0000_0000
            assertTrue(isFieldInputInvalid(path, "99999999999999"))
            assertTrue(isFieldInputInvalid(path, "not-a-number"))
            assertTrue(isFieldInputInvalid(path, ""))
        }
    }

    @Test
    fun `non-43284 leaves keep the historical numeric rule`() {
        /* Existing fields may be negative and are only required to parse. */
        assertFalse(isFieldInputInvalid("offset.init_task", "-1"))
        assertFalse(isFieldInputInvalid("cred.ref0_image", "-274696158080"))
        assertTrue(isFieldInputInvalid("offset.init_task", "abc"))
        assertTrue(isFieldInputInvalid("offset.init_task", ""))
    }
}
