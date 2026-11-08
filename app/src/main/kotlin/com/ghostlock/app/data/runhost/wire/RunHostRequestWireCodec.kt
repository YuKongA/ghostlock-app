package com.ghostlock.app.data.runhost.wire

import android.os.ParcelFileDescriptor
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostFinishItemRequest
import com.ghostlock.app.core.runhost.RunHostFinishKind
import com.ghostlock.app.core.runhost.RunHostFinishSeverity
import com.ghostlock.app.core.runhost.RunHostPayloadAcceptance
import com.ghostlock.app.core.runhost.RunHostPayloadItemRequest
import com.ghostlock.app.core.runhost.RunHostPayloadKind
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.data.run.OpenedRunInputArtifact
import com.ghostlock.app.run.FinishItemParcel
import com.ghostlock.app.run.PayloadItemParcel
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunInputArtifactParcel

/**
 * Converts a generated AIDL request into the normalized in-process contract.
 *
 * @return Immutable request metadata; descriptor ownership remains with the Binder input until duplicated.
 * @throws IllegalArgumentException When a stable enum value is unknown.
 */
internal fun RunHostRequest.toModel(): RunHostRequestModel = RunHostRequestModel(
    runId = runId,
    generation = generation,
    backendKey = backendKey,
    profileArtifactId = profileArtifactId,
    payloadItems = payloadItems.map(PayloadItemParcel::toModel),
    finishItems = finishItems.map(FinishItemParcel::toModel),
    artifacts = artifacts.map(RunInputArtifactParcel::toModel),
)

/**
 * Duplicates every request descriptor so the RunHost owns its lifetime independently of Binder input objects.
 *
 * @param model Decoded request metadata in the same artifact order as this wire request.
 * @return Fully owned descriptor list.
 * @throws IllegalArgumentException When the wire and decoded artifact counts differ or a descriptor is null.
 */
internal fun RunHostRequest.duplicateDescriptors(
    model: RunHostRequestModel,
): List<OpenedRunInputArtifact> {
    require(artifacts.size == model.artifacts.size) { "artifact count changed during decode" }
    val owned = mutableListOf<OpenedRunInputArtifact>()
    try {
        artifacts.zip(model.artifacts).forEach { (wire, metadata) ->
            val source = requireNotNull(wire.fd) { "artifact descriptor is null" }
            owned += OpenedRunInputArtifact(metadata, ParcelFileDescriptor.dup(source.fileDescriptor))
        }
        return owned
    } catch (problem: Throwable) {
        owned.forEach { artifact -> runCatching { artifact.descriptor.close() } }
        throw problem
    }
}

/** Decodes one artifact metadata record and rejects unknown enum values. */
private fun RunInputArtifactParcel.toModel(): RunHostArtifactRequest = RunHostArtifactRequest(
    logicalId = logicalId,
    kind = RunHostArtifactKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown artifact kind"),
    byteCount = byteCount,
    sha256 = sha256,
)

/** Decodes one Payload item and rejects unknown kind or acceptance values. */
private fun PayloadItemParcel.toModel(): RunHostPayloadItemRequest = RunHostPayloadItemRequest(
    itemId = itemId,
    kind = RunHostPayloadKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown Payload kind"),
    required = required,
    artifactId = artifactId,
    args = args.toList(),
    acceptance = RunHostPayloadAcceptance.entries.firstOrNull { candidate ->
        candidate.wireValue == acceptanceKind
    } ?: throw IllegalArgumentException("unknown Payload acceptance"),
)

/** Decodes one Finish item and rejects unknown kind or severity values. */
private fun FinishItemParcel.toModel(): RunHostFinishItemRequest = RunHostFinishItemRequest(
    itemId = itemId,
    kind = RunHostFinishKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown Finish kind"),
    severity = RunHostFinishSeverity.entries.firstOrNull { candidate -> candidate.wireValue == severity }
        ?: throw IllegalArgumentException("unknown Finish severity"),
    alwaysRun = alwaysRun,
    artifactId = artifactId,
    args = args.toList(),
)
