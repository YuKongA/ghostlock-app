package com.ghostlock.app.backend.backends.cve43284glued

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Unit contract tests for DFRoot argv encoding and the run-scoped IPsec close stack. */
class IpSecCliArgumentsTest {
    @Test
    fun parserVectorUsesFixedFlagsUnsignedSpiAndLowercaseKey() {
        val key = ByteArray(32) { it.toByte() }

        val arguments = IpSecCliArguments.create(
            encapPort = 4500,
            senderPort = 49152,
            spi = 0xFEDCBA98.toInt(),
            aesKey = key,
        )

        assertEquals(
            listOf(
                "--encap-port", "4500",
                "--sender-port", "49152",
                "--spi", "4275878552",
                "--aes-key", "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
            ),
            arguments,
        )
    }

    @Test
    fun rejectsInvalidPortZeroSpiAndWrongKeyLength() {
        val key = ByteArray(32)

        assertRejected { IpSecCliArguments.create(0, 49152, 1, key) }
        assertRejected { IpSecCliArguments.create(4500, 65536, 1, key) }
        assertRejected { IpSecCliArguments.create(4500, 49152, 0, key) }
        assertRejected { IpSecCliArguments.create(4500, 49152, 1, ByteArray(31)) }
    }

    @Test
    fun closeStackReleasesInReverseOrderContinuesAfterFailureAndRunsOnce() {
        val closed = mutableListOf<String>()
        val scope = ReverseCloseScope()
        scope.add("encapsulation") { closed += "encapsulation" }
        scope.add("spi") { closed += "spi" }
        scope.add("transform") {
            closed += "transform"
            throw IllegalStateException("fixture")
        }

        val failures = scope.close()

        assertEquals(listOf("transform", "spi", "encapsulation"), closed)
        assertEquals(listOf("transform:IllegalStateException"), failures)
        assertTrue(scope.close().isEmpty())
        assertEquals(listOf("transform", "spi", "encapsulation"), closed)
    }

    private fun assertRejected(block: () -> Unit) {
        try {
            block()
            throw AssertionError("Expected invalid IPsec input to be rejected")
        } catch (_: IllegalArgumentException) {
            // Expected: invalid port/SPI/key input must fail before exec.
        }
    }
}
