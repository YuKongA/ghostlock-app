package com.ghostlock.app.core.runhost

import com.ghostlock.app.domain.run.RunCapability
import java.security.MessageDigest

/** Stable wire kinds for staged input artifacts. */
enum class RunHostArtifactKind(val wireValue: Int) {
    PROFILE(1),
    USER_SCRIPT(2),
    EXECUTABLE(3),
    LKM(4),
    DATA(5),
}

/** Stable Payload item kinds. */
enum class RunHostPayloadKind(val wireValue: Int) {
    COMMAND(1),
    SCRIPT(2),
    ROOT_MANAGER(3),
    LKM(4),
    DETACHED_PROCESS(5),
}

/** Stable Payload acceptance facts. */
enum class RunHostPayloadAcceptance(val wireValue: Int) {
    EXIT_ZERO(1),
    DAEMON_ACK(2),
    MODULE_PRESENT(3),
    TAKEOVER_ACK(4),
}

/** Stable Finish item kinds. */
enum class RunHostFinishKind(val wireValue: Int) {
    COMMAND(1),
    SCRIPT(2),
    REBOOT(3),
}

/** Stable impact of one Finish failure. */
enum class RunHostFinishSeverity(val wireValue: Int) {
    NORMAL(1),
    SAFETY_CRITICAL(2),
}

/** Metadata for one PFD-backed regular-file input. */
data class RunHostArtifactRequest(
    val logicalId: String,
    val kind: RunHostArtifactKind,
    val byteCount: Long,
    val sha256: String,
)

/** One typed Payload request. */
data class RunHostPayloadItemRequest(
    val itemId: String,
    val kind: RunHostPayloadKind,
    val required: Boolean,
    val artifactId: String?,
    val args: List<String>,
    val acceptance: RunHostPayloadAcceptance,
)

/** One typed Finish request. */
data class RunHostFinishItemRequest(
    val itemId: String,
    val kind: RunHostFinishKind,
    val severity: RunHostFinishSeverity,
    val alwaysRun: Boolean,
    val artifactId: String?,
    val args: List<String>,
)

/** Normalized request used for validation and semantic fingerprinting. */
data class RunHostRequestModel(
    val runId: String,
    val generation: Long,
    val backendKey: String,
    val profileArtifactId: String,
    val payloadItems: List<RunHostPayloadItemRequest>,
    val finishItems: List<RunHostFinishItemRequest>,
    val artifacts: List<RunHostArtifactRequest>,
)

/** Result of one deterministic request validation pass. */
sealed interface RunHostRequestValidation {
    /** Request is valid for the examined validation phase. */
    data object Valid : RunHostRequestValidation

    /** Request is invalid with a stable host code and diagnostic-only detail. */
    data class Invalid(
        val code: RunHostCode,
        val detail: String,
    ) : RunHostRequestValidation
}

/** Pure structural and capability-specific request validator. */
object RunHostRequestValidator {
    /**
     * Validates grammar, counts, references, and item conjunctions before registry lookup.
     *
     * @param request Normalized request.
     * @return [RunHostRequestValidation.Valid] or the first deterministic invalid fact.
     */
    fun validateStructure(request: RunHostRequestModel): RunHostRequestValidation {
        if (!RunHostProtocol.isRunId(request.runId) || request.generation <= 0L) {
            return invalid("invalid run identity")
        }
        if (!RunHostProtocol.isBackendKey(request.backendKey)) return invalid("invalid backend key")
        if (!RunHostProtocol.isLogicalId(request.profileArtifactId)) return invalid("invalid profile artifact id")
        if (request.artifacts.size > RunHostProtocol.MAX_ARTIFACTS) return limit("too many artifacts")
        if (request.payloadItems.size > RunHostProtocol.MAX_PAYLOAD_ITEMS) return limit("too many Payload items")
        if (request.finishItems.size > RunHostProtocol.MAX_FINISH_ITEMS) return limit("too many Finish items")

        val allIds = buildList {
            addAll(request.artifacts.map(RunHostArtifactRequest::logicalId))
            addAll(request.payloadItems.map(RunHostPayloadItemRequest::itemId))
            addAll(request.finishItems.map(RunHostFinishItemRequest::itemId))
        }
        if (allIds.any { value -> !RunHostProtocol.isLogicalId(value) }) return invalid("invalid logical id")
        if (allIds.toSet().size != allIds.size) return invalid("duplicate request-global logical id")

        var totalBytes = 0L
        request.artifacts.forEach { artifact ->
            if (artifact.byteCount < 0L) return invalid("negative artifact size")
            if (artifact.byteCount > RunHostProtocol.MAX_ARTIFACT_BYTES) return limit("artifact too large")
            if (!RunHostProtocol.isSha256(artifact.sha256)) return invalid("invalid artifact digest")
            if (totalBytes > RunHostProtocol.MAX_TOTAL_ARTIFACT_BYTES - artifact.byteCount) {
                return limit("total artifact bytes exceeded")
            }
            totalBytes += artifact.byteCount
        }
        val artifactById = request.artifacts.associateBy(RunHostArtifactRequest::logicalId)
        if (artifactById[request.profileArtifactId]?.kind != RunHostArtifactKind.PROFILE) {
            return invalid("profileArtifactId must reference one PROFILE artifact")
        }

        request.payloadItems.forEach { item ->
            validateArgs(item.args)?.let { result -> return result }
            val artifactKind = item.artifactId?.let { id -> artifactById[id]?.kind }
            if (item.artifactId != null && artifactKind == null) return invalid("unknown Payload artifact")
            val valid = when (item.kind) {
                RunHostPayloadKind.COMMAND ->
                    item.acceptance == RunHostPayloadAcceptance.EXIT_ZERO &&
                        item.artifactId == null && item.args.isNotEmpty()

                RunHostPayloadKind.SCRIPT ->
                    item.acceptance == RunHostPayloadAcceptance.EXIT_ZERO &&
                        artifactKind == RunHostArtifactKind.USER_SCRIPT

                RunHostPayloadKind.ROOT_MANAGER ->
                    item.acceptance == RunHostPayloadAcceptance.DAEMON_ACK &&
                        artifactKind == RunHostArtifactKind.EXECUTABLE

                RunHostPayloadKind.LKM ->
                    item.acceptance == RunHostPayloadAcceptance.MODULE_PRESENT &&
                        artifactKind == RunHostArtifactKind.LKM

                RunHostPayloadKind.DETACHED_PROCESS ->
                    item.acceptance == RunHostPayloadAcceptance.TAKEOVER_ACK &&
                        artifactKind in setOf(RunHostArtifactKind.EXECUTABLE, RunHostArtifactKind.USER_SCRIPT)
            }
            if (!valid) return invalid("invalid Payload kind/acceptance/artifact/args conjunction")
        }

        val rebootIndexes = mutableListOf<Int>()
        request.finishItems.forEachIndexed { index, item ->
            validateArgs(item.args)?.let { result -> return result }
            val artifactKind = item.artifactId?.let { id -> artifactById[id]?.kind }
            if (item.artifactId != null && artifactKind == null) return invalid("unknown Finish artifact")
            val valid = when (item.kind) {
                RunHostFinishKind.COMMAND -> item.artifactId == null && item.args.isNotEmpty()
                RunHostFinishKind.SCRIPT -> artifactKind == RunHostArtifactKind.USER_SCRIPT
                RunHostFinishKind.REBOOT -> {
                    rebootIndexes += index
                    item.artifactId == null && item.args.isEmpty() && item.alwaysRun
                }
            }
            if (!valid) return invalid("invalid Finish kind/artifact/args conjunction")
        }
        if (rebootIndexes.size > 1) return invalid("multiple reboot Finish items")
        if (rebootIndexes.singleOrNull()?.let { index -> index != request.finishItems.lastIndex } == true) {
            return invalid("reboot Finish item must be last")
        }
        return RunHostRequestValidation.Valid
    }

    /**
     * Applies immutable-registry capability shape rules after exact backend lookup.
     *
     * @param request Structurally valid request.
     * @param capability Capability projected only from BackendDescriptor.
     */
    fun validateCapability(
        request: RunHostRequestModel,
        capability: RunCapability,
    ): RunHostRequestValidation = when (capability) {
        RunCapability.PRODUCT_ROOT -> if (
            request.payloadItems.isNotEmpty() && request.payloadItems.any(RunHostPayloadItemRequest::required)
        ) {
            RunHostRequestValidation.Valid
        } else {
            invalid("product backend requires a non-empty Payload with at least one required item")
        }

        RunCapability.DIAGNOSTIC_ONLY -> if (
            request.payloadItems.isEmpty() && request.finishItems.isEmpty()
        ) {
            RunHostRequestValidation.Valid
        } else {
            invalid("diagnostic backend rejects Payload and Finish")
        }

        RunCapability.UNRESOLVED -> invalid("registry capability is unresolved")
    }

    /** @return Null for valid args, otherwise a stable invalid/limit result. */
    private fun validateArgs(args: List<String>): RunHostRequestValidation? {
        if (args.size > RunHostProtocol.MAX_ITEM_ARGS) return limit("too many item arguments")
        if (args.any { value -> value.toByteArray(Charsets.UTF_8).size > RunHostProtocol.MAX_ARG_BYTES }) {
            return limit("item argument too large")
        }
        if (args.any(String::isEmpty)) return invalid("empty argv token")
        return null
    }

    /** Stable structural rejection. */
    private fun invalid(detail: String): RunHostRequestValidation.Invalid =
        RunHostRequestValidation.Invalid(RunHostCode.INVALID_REQUEST, detail)

    /** Stable hard-limit rejection. */
    private fun limit(detail: String): RunHostRequestValidation.Invalid =
        RunHostRequestValidation.Invalid(RunHostCode.LIMIT_EXCEEDED, detail)
}

/**
 * Returns the normalized semantic fingerprint shared by App and Shizuku hosts.
 *
 * Descriptor numbers and raw Parcel bytes are intentionally absent; artifact metadata and ordered typed fields
 * are the complete idempotency authority.
 */
internal fun RunHostRequestModel.semanticFingerprint(): String {
    val normalized = buildString {
        append(runId).append('\n')
        append(generation).append('\n')
        append(backendKey).append('\n')
        append(profileArtifactId).append('\n')
        artifacts.forEach { value ->
            append("a|").append(value.logicalId).append('|').append(value.kind.wireValue)
                .append('|').append(value.byteCount).append('|').append(value.sha256).append('\n')
        }
        payloadItems.forEach { value ->
            append("p|").append(value.itemId).append('|').append(value.kind.wireValue)
                .append('|').append(value.required).append('|').append(value.artifactId.orEmpty())
                .append('|').append(value.acceptance.wireValue).append('|')
                .append(value.args.joinToString(separator = "\u001f")).append('\n')
        }
        finishItems.forEach { value ->
            append("f|").append(value.itemId).append('|').append(value.kind.wireValue)
                .append('|').append(value.severity.wireValue).append('|').append(value.alwaysRun)
                .append('|').append(value.artifactId.orEmpty()).append('|')
                .append(value.args.joinToString(separator = "\u001f")).append('\n')
        }
    }
    return MessageDigest.getInstance("SHA-256")
        .digest(normalized.toByteArray(Charsets.UTF_8))
        .joinToString(separator = "") { byte ->
            (byte.toInt() and 0xff).toString(16).padStart(2, '0')
        }
}
