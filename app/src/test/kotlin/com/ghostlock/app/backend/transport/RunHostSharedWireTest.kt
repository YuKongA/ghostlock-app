package com.ghostlock.app.backend.transport

import java.util.concurrent.atomic.AtomicLong
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/** Shared normalized request semantics used by both App and Shizuku clients. */
class RunHostSharedWireTest {
    /** Equivalent typed requests yield one fingerprint independent of transport. */
    @Test
    fun semanticFingerprintIsTransportNeutral() {
        val first = request()
        val second = request().copy()
        assertEquals(first.semanticFingerprint(), second.semanticFingerprint())
    }

    /** Ordered typed-field changes alter the idempotency authority. */
    @Test
    fun semanticFingerprintChangesWithRequestMeaning() {
        val first = request()
        val second = first.copy(generation = 2L)
        assertNotEquals(first.semanticFingerprint(), second.semanticFingerprint())
    }

    /** Both host producers share the same unbounded positive Long reservation contract. */
    @Test
    fun sharedLogSequenceSupports16384Records() {
        val app = AtomicLong(0L)
        val shizuku = AtomicLong(0L)
        repeat(16_384) { index ->
            val expected = index.toLong() + 1L
            assertEquals(expected, reserveRunHostLogSequence(app))
            assertEquals(expected, reserveRunHostLogSequence(shizuku))
        }
    }

    private fun request(): RunHostRequestModel = RunHostRequestModel(
        runId = "0".repeat(32),
        generation = 1L,
        backendKey = "DIAGNOSE",
        profileArtifactId = "profile",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = listOf(
            RunHostArtifactRequest(
                logicalId = "profile",
                kind = RunHostArtifactKind.PROFILE,
                byteCount = 3L,
                sha256 = "a".repeat(64),
            ),
        ),
    )
}
