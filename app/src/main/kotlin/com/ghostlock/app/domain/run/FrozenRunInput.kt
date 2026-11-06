package com.ghostlock.app.domain.run

import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.payload.PayloadDefinition

/** Stable kind of a read-only input artifact. */
enum class RunInputArtifactKind(val wireValue: Int) {
    PROFILE(1),
    USER_SCRIPT(2),
    EXECUTABLE(3),
    LKM(4),
    DATA(5),
}

/**
 * Immutable identity of one input that will be opened and staged by a RunHost adapter.
 *
 * @property logicalId Request-global logical identifier.
 * @property kind Stable artifact kind.
 * @property byteCount Exact regular-file size.
 * @property sha256 Lowercase SHA-256 of the opened source object.
 * @property sourceToken Opaque application-side token resolved once by the data adapter; never put on wire.
 */
data class RunInputArtifact(
    val logicalId: String,
    val kind: RunInputArtifactKind,
    val byteCount: Long,
    val sha256: String,
    val sourceToken: String,
)

/**
 * Frozen binding between one Payload item and a staged executable/data artifact.
 *
 * @property itemId Exact Payload item identity.
 * @property artifactId Logical artifact identity carried on the typed RunHost wire.
 * @property args Exact argv tokens derived before descriptor transfer.
 */
data class FrozenArtifactInvocation(
    val itemId: String,
    val artifactId: String,
    val args: List<String>,
)

/**
 * Canonical profile identity frozen for one run.
 *
 * @property artifactId Logical ID of the single profile artifact.
 * @property schemaVersion Canonical GLK profile schema version; only version three is accepted.
 * @property sha256 Lowercase digest bound to the corresponding artifact identity.
 */
data class ProfileSnapshot(
    val artifactId: String,
    val schemaVersion: Int,
    val sha256: String,
) {
    init {
        require(schemaVersion == CURRENT_SCHEMA_VERSION) { "only GLK profile version three is supported" }
    }

    companion object {
        /** Current and only accepted profile version. */
        const val CURRENT_SCHEMA_VERSION: Int = 3
    }
}

/**
 * Complete immutable application input captured before process binding or staging.
 *
 * @property runId Exactly 32 lowercase hexadecimal characters.
 * @property generation Positive process-monotonic generation.
 * @property backendKey Stable immutable-registry key.
 * @property profile Canonical profile snapshot.
 * @property payload Ordered product target definition.
 * @property finish Ordered post-Payload definition.
 * @property inputArtifacts Read-only inputs opened by the application data adapter.
 * @property artifactInvocations Frozen item-to-artifact bindings and exact argv.
 */
data class FrozenRunInput(
    val runId: String,
    val generation: Long,
    val backendKey: String,
    val profile: ProfileSnapshot,
    val payload: PayloadDefinition,
    val finish: FinishDefinition,
    val inputArtifacts: List<RunInputArtifact>,
    val artifactInvocations: List<FrozenArtifactInvocation> = emptyList(),
)
