package com.ghostlock.app.core.rootscript

import java.nio.charset.StandardCharsets

/** Stable normalized Payload operation kinds consumed by the sole root-script renderer. */
enum class RootScriptPayloadKind {
    COMMAND,
    SCRIPT,
    ROOT_MANAGER,
    LKM,
    DETACHED_PROCESS,
}

/** Stable normalized acceptance contracts evaluated after one Payload operation. */
enum class RootScriptPayloadAcceptance {
    EXIT_ZERO,
    DAEMON_ACK,
    MODULE_PRESENT,
    TAKEOVER_ACK,
}

/** Stable normalized Finish operation kinds. */
enum class RootScriptFinishKind {
    COMMAND,
    SCRIPT,
    REBOOT,
}

/** Stable normalized Finish failure impact. */
enum class RootScriptFinishSeverity {
    NORMAL,
    SAFETY_CRITICAL,
}

/**
 * One fully resolved Payload operation.
 *
 * @property itemId Frozen result identity.
 * @property kind Validated operation kind.
 * @property required Whether a non-success result triggers Payload fail-fast.
 * @property stagedPath Host-local staged object when the kind requires one.
 * @property args Exact argv tokens after the freeze boundary.
 * @property acceptance Independent success evidence.
 */
data class RootScriptPayloadExecution(
    val itemId: String,
    val kind: RootScriptPayloadKind,
    val required: Boolean,
    val stagedPath: String?,
    val args: List<String>,
    val acceptance: RootScriptPayloadAcceptance,
)

/**
 * One fully resolved Finish operation.
 *
 * @property itemId Frozen result identity.
 * @property kind Validated operation kind.
 * @property severity Product-terminal impact of failure.
 * @property alwaysRun Whether the item still runs after required Payload failure.
 * @property stagedPath Host-local script path when [kind] is [RootScriptFinishKind.SCRIPT].
 * @property args Exact process argv or script arguments.
 */
data class RootScriptFinishExecution(
    val itemId: String,
    val kind: RootScriptFinishKind,
    val severity: RootScriptFinishSeverity,
    val alwaysRun: Boolean,
    val stagedPath: String?,
    val args: List<String>,
)

/**
 * Sole normalized input accepted by [RootScriptRenderer].
 *
 * @property runId Exact frozen run identity.
 * @property generation Positive run generation.
 * @property payload Ordered, fully resolved Payload operations.
 * @property finish Ordered, fully resolved Finish operations.
 * @property retainedArtifacts Explicit host-local evidence paths.
 * @note Catalog lookup, package discovery, KMI derivation, and URI resolution happen before this plan exists.
 */
data class RootScriptExecutionPlan(
    val runId: String,
    val generation: Long,
    val payload: List<RootScriptPayloadExecution>,
    val finish: List<RootScriptFinishExecution>,
    val retainedArtifacts: List<String> = emptyList(),
) {
    /**
     * Validates the sole renderer/decoder/composer plan shape in one authority.
     *
     * @throws IllegalArgumentException For invalid identity, ordering, argv, or kind conjunctions.
     */
    fun validate() {
        require(RUN_ID_PATTERN.matches(runId)) { "invalid run id" }
        require(generation > 0L) { "invalid generation" }
        require(payload.size in 1..64 && payload.any(RootScriptPayloadExecution::required)) {
            "invalid Payload shape"
        }
        require(finish.size <= 32) { "invalid Finish shape" }
        val ids = HashSet<String>()
        (payload.map(RootScriptPayloadExecution::itemId) +
            finish.map(RootScriptFinishExecution::itemId)).forEach { id ->
            require(ITEM_ID_PATTERN.matches(id) && ids.add(id)) { "invalid or duplicate item id" }
        }
        payload.forEach { item ->
            validateArgs(item.args)
            when (item.kind) {
                RootScriptPayloadKind.COMMAND -> require(
                    item.stagedPath == null && item.args.isNotEmpty() &&
                        item.acceptance == RootScriptPayloadAcceptance.EXIT_ZERO,
                ) { "invalid command Payload" }
                RootScriptPayloadKind.SCRIPT -> require(
                    !item.stagedPath.isNullOrBlank() &&
                        item.acceptance == RootScriptPayloadAcceptance.EXIT_ZERO,
                ) { "invalid script Payload" }
                RootScriptPayloadKind.ROOT_MANAGER -> require(
                    !item.stagedPath.isNullOrBlank() && item.args.isNotEmpty() &&
                        item.acceptance == RootScriptPayloadAcceptance.DAEMON_ACK,
                ) { "invalid root-manager Payload" }
                RootScriptPayloadKind.LKM -> require(
                    !item.stagedPath.isNullOrBlank() &&
                        item.acceptance == RootScriptPayloadAcceptance.MODULE_PRESENT,
                ) { "invalid LKM Payload" }
                RootScriptPayloadKind.DETACHED_PROCESS -> require(
                    !item.stagedPath.isNullOrBlank() &&
                        item.acceptance == RootScriptPayloadAcceptance.TAKEOVER_ACK,
                ) { "invalid detached Payload" }
            }
        }
        finish.forEachIndexed { index, item ->
            validateArgs(item.args)
            when (item.kind) {
                RootScriptFinishKind.COMMAND -> require(
                    item.stagedPath == null && item.args.isNotEmpty(),
                ) { "invalid command Finish" }
                RootScriptFinishKind.SCRIPT -> require(!item.stagedPath.isNullOrBlank()) {
                    "invalid script Finish"
                }
                RootScriptFinishKind.REBOOT -> require(
                    index == finish.lastIndex && item.alwaysRun &&
                        item.stagedPath == null && item.args.isEmpty(),
                ) { "invalid reboot Finish" }
            }
        }
    }

    /** Validates bounded UTF-8 argv tokens. */
    private fun validateArgs(args: List<String>) {
        require(args.size <= 32 && args.none(String::isEmpty)) { "invalid argv" }
        require(args.all { argument -> argument.toByteArray(StandardCharsets.UTF_8).size <= 4096 }) {
            "argv token too large"
        }
    }

    private companion object {
        val RUN_ID_PATTERN: Regex = Regex("^[0-9a-f]{32}$")
        val ITEM_ID_PATTERN: Regex = Regex("^[a-z][a-z0-9._-]{0,63}$")
    }
}
