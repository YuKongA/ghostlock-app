package com.ghostlock.app.backend.transport

import com.ghostlock.app.domain.run.RunCapability
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Structural and capability-shape cases for the normalized typed request. */
class RunHostRequestValidationTest {
    /** Diagnostic requests carry only one profile and no product work. */
    @Test
    fun diagnosticShapeAcceptsProfileOnly() {
        val request = baseRequest()
        assertEquals(RunHostRequestValidation.Valid, RunHostRequestValidator.validateStructure(request))
        assertEquals(
            RunHostRequestValidation.Valid,
            RunHostRequestValidator.validateCapability(request, RunCapability.DIAGNOSTIC_ONLY),
        )
    }

    /** Product requests require at least one required Payload item. */
    @Test
    fun productShapeRejectsEmptyPayload() {
        val result = RunHostRequestValidator.validateCapability(baseRequest(), RunCapability.PRODUCT_ROOT)
        assertTrue(result is RunHostRequestValidation.Invalid)
        assertEquals(RunHostCode.INVALID_REQUEST, (result as RunHostRequestValidation.Invalid).code)
    }

    /** Request-global IDs cannot collide across artifact and item collections. */
    @Test
    fun duplicateLogicalIdIsRejected() {
        val request = baseRequest().copy(
            payloadItems = listOf(
                RunHostPayloadItemRequest(
                    itemId = "profile",
                    kind = RunHostPayloadKind.COMMAND,
                    required = true,
                    artifactId = null,
                    args = listOf("id"),
                    acceptance = RunHostPayloadAcceptance.EXIT_ZERO,
                ),
            ),
        )
        val result = RunHostRequestValidator.validateStructure(request)
        assertTrue(result is RunHostRequestValidation.Invalid)
        assertEquals(RunHostCode.INVALID_REQUEST, (result as RunHostRequestValidation.Invalid).code)
    }

    /** Aggregate artifact bytes use an overflow-safe limit check. */
    @Test
    fun aggregateArtifactLimitIsRejected() {
        val first = RunHostArtifactRequest(
            logicalId = "profile",
            kind = RunHostArtifactKind.PROFILE,
            byteCount = RunHostProtocol.MAX_ARTIFACT_BYTES,
            sha256 = "0".repeat(64),
        )
        val request = baseRequest().copy(
            artifacts = listOf(
                first,
                first.copy(logicalId = "data-a", kind = RunHostArtifactKind.DATA),
                first.copy(logicalId = "data-b", kind = RunHostArtifactKind.DATA),
            ),
        )
        val result = RunHostRequestValidator.validateStructure(request)
        assertTrue(result is RunHostRequestValidation.Invalid)
        assertEquals(RunHostCode.LIMIT_EXCEEDED, (result as RunHostRequestValidation.Invalid).code)
    }

    private fun baseRequest(): RunHostRequestModel = RunHostRequestModel(
        runId = "0".repeat(32),
        generation = 1L,
        backendKey = "DEBUG-PROBE",
        profileArtifactId = "profile",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = listOf(
            RunHostArtifactRequest(
                logicalId = "profile",
                kind = RunHostArtifactKind.PROFILE,
                byteCount = 0L,
                sha256 = "0".repeat(64),
            ),
        ),
    )
}
