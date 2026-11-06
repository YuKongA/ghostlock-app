package com.ghostlock.app.core.rootscript

import java.io.File
import java.nio.charset.StandardCharsets

/** Strict GLR1 line-protocol codec. */
object RootScriptResultCodec {
    private const val MAX_RESULT_BYTES: Int = 256 * 1024
    private val runIdPattern = Regex("^[0-9a-f]{32}$")

    /**
     * Reads result bytes only after same-open-object verification, then performs strict decoding.
     *
     * @param file Pre-created result path.
     * @param identity Pre-execution object identity.
     * @param plan The same normalized plan used by the renderer.
     * @return Complete typed result.
     * @throws IllegalArgumentException On replacement, truncation, malformed content, or policy mismatch.
     */
    fun decode(
        file: File,
        identity: PublishedArtifactIdentity,
        plan: RootScriptExecutionPlan,
    ): RootScriptResult {
        val bytes = identity.readSameObject(file)
            ?: throw IllegalArgumentException("result artifact identity mismatch")
        return decode(bytes, plan)
    }

    /**
     * Decodes a complete ASCII GLR1 file and validates ordering plus Payload/Finish policy.
     *
     * @param bytes Complete result bytes.
     * @param plan The same normalized plan used by the renderer.
     * @return Complete typed result.
     */
    fun decode(bytes: ByteArray, plan: RootScriptExecutionPlan): RootScriptResult {
        require(bytes.isNotEmpty() && bytes.size <= MAX_RESULT_BYTES) { "GLR1 size" }
        require(bytes.all { byte -> byte.toInt() in 0..0x7f && byte.toInt() != 0 && byte.toInt() != 13 }) {
            "GLR1 must be ASCII LF-only"
        }
        require(bytes.last().toInt() == 10) { "GLR1 must end with LF" }
        plan.validate()
        require(runIdPattern.matches(plan.runId)) { "invalid expected identity" }
        val text = bytes.toString(StandardCharsets.US_ASCII)
        val lines = text.split('\n').dropLast(1)
        val payloadItems = plan.payload
        val finishItems = plan.finish
        val expectedLineCount = 3 + (payloadItems.size + finishItems.size) * 4 + 2
        require(lines.size == expectedLineCount) { "GLR1 line count" }
        var cursor = 0
        require(lines[cursor++] == "GLR1") { "GLR1 magic" }
        require(lines[cursor++] == "run=${plan.runId}") { "GLR1 run mismatch" }
        val generation = parsePositiveDecimal(lines[cursor++].removeExactPrefix("generation="))
        require(generation == plan.generation) { "GLR1 generation mismatch" }
        val decodedPayload = payloadItems.map { item ->
            parseItem(lines, cursor, "payload", item.itemId).also { cursor += 4 }
        }
        val decodedFinish = finishItems.map { item ->
            parseItem(lines, cursor, "finish", item.itemId).also { cursor += 4 }
        }
        val handoff = parseZeroOrOne(lines[cursor++].removeExactPrefix("handoff=")) == 1
        require(lines[cursor] == "complete=1") { "GLR1 incomplete" }
        validatePayload(payloadItems, decodedPayload)
        validateFinish(payloadItems, finishItems, decodedPayload, decodedFinish, handoff)
        return RootScriptResult(
            runId = plan.runId,
            generation = generation,
            payloadItems = decodedPayload,
            finishItems = decodedFinish,
            durableHandoff = handoff,
        )
    }

    /** Parses four contiguous lines for one exact phase and id. */
    private fun parseItem(
        lines: List<String>,
        start: Int,
        phase: String,
        itemId: String,
    ): RootScriptItemResult {
        val prefix = "$phase.$itemId."
        val stateValue = parseNonNegativeDecimal(lines[start].removeExactPrefix(prefix + "state="))
        val reasonValue = parseNonNegativeDecimal(lines[start + 1].removeExactPrefix(prefix + "reason="))
        val exitKindValue = parseNonNegativeDecimal(lines[start + 2].removeExactPrefix(prefix + "exit_kind="))
        val exitValue = parseNonNegativeDecimal(lines[start + 3].removeExactPrefix(prefix + "exit_value="))
        val result = RootScriptItemResult(
            itemId = itemId,
            state = RootScriptItemState.fromWireValue(stateValue)
                ?: throw IllegalArgumentException("unknown GLR1 item state"),
            reason = RootScriptResultReason.fromWireValue(reasonValue)
                ?: throw IllegalArgumentException("unknown or reserved GLR1 reason"),
            exitKind = RootScriptExitKind.fromWireValue(exitKindValue)
                ?: throw IllegalArgumentException("unknown GLR1 exit kind"),
            exitValue = exitValue,
        )
        require(validConjunction(result)) { "invalid GLR1 item conjunction" }
        return result
    }

    /** Enforces the wire state/reason/exit conjunction table. */
    private fun validConjunction(result: RootScriptItemResult): Boolean = when (result.state) {
        RootScriptItemState.NotRun ->
            result.reason == RootScriptResultReason.NotStarted && result.hasNoExit()
        RootScriptItemState.Dispatching ->
            result.reason == RootScriptResultReason.None && result.hasNoExit()
        RootScriptItemState.Success ->
            result.reason == RootScriptResultReason.None &&
                (result.hasNoExit() || result.hasProcessExit(0))
        RootScriptItemState.Failed -> when (result.reason) {
            RootScriptResultReason.ProcessExitNonzero ->
                result.exitKind == RootScriptExitKind.ProcessExit && result.exitValue in 1..255
            RootScriptResultReason.Signalled ->
                result.exitKind == RootScriptExitKind.Signal && result.exitValue in 1..127
            RootScriptResultReason.Rejected,
            RootScriptResultReason.IoFailure,
            RootScriptResultReason.Timeout,
            -> result.hasNoExit()
            else -> false
        }
        RootScriptItemState.SkippedDependency ->
            result.reason == RootScriptResultReason.NotStarted && result.hasNoExit()
        RootScriptItemState.Unknown ->
            result.reason in setOf(
                RootScriptResultReason.HostDied,
                RootScriptResultReason.MalformedResult,
                RootScriptResultReason.InternalContractFailure,
                RootScriptResultReason.AckMissing,
            ) && result.hasNoExit()
        RootScriptItemState.Cancelled ->
            result.reason == RootScriptResultReason.Cancelled && result.hasNoExit()
    }

    /** Enforces ordered required-item fail-fast without repairing any partial prefix. */
    private fun validatePayload(
        items: List<RootScriptPayloadExecution>,
        results: List<RootScriptItemResult>,
    ) {
        require(items.size == results.size && items.isNotEmpty() && items.any { it.required }) {
            "invalid frozen Payload"
        }
        var blocked = false
        items.zip(results).forEach { (item, result) ->
            if (blocked) {
                require(result.state == RootScriptItemState.SkippedDependency) {
                    "Payload continued after fail-fast"
                }
                return@forEach
            }
            require(result.state !in setOf(
                RootScriptItemState.NotRun,
                RootScriptItemState.Dispatching,
                RootScriptItemState.SkippedDependency,
            )) { "illegal completed Payload state" }
            if (item.required && result.state in setOf(
                    RootScriptItemState.Failed,
                    RootScriptItemState.Unknown,
                    RootScriptItemState.Cancelled,
                )
            ) {
                blocked = true
            }
        }
    }

    /** Enforces Finish best-effort, always-run, and durable reboot-handoff rules. */
    private fun validateFinish(
        payload: List<RootScriptPayloadExecution>,
        finish: List<RootScriptFinishExecution>,
        payloadResults: List<RootScriptItemResult>,
        finishResults: List<RootScriptItemResult>,
        durableHandoff: Boolean,
    ) {
        val rebootIndex = finish.indexOfFirst { item -> item.kind == RootScriptFinishKind.REBOOT }
        if (rebootIndex >= 0) {
            require(rebootIndex == finish.lastIndex && durableHandoff) {
                "reboot requires durable handoff"
            }
        } else {
            require(!durableHandoff) { "handoff without reboot" }
        }
        val requiredPayloadSucceeded = payload.zip(payloadResults)
            .filter { (item, _) -> item.required }
            .all { (_, result) -> result.state == RootScriptItemState.Success }
        finish.zip(finishResults).forEachIndexed { index, (item, result) ->
            if (item.kind == RootScriptFinishKind.REBOOT && durableHandoff) {
                require(index == finish.lastIndex && result.state == RootScriptItemState.Dispatching) {
                    "invalid reboot handoff"
                }
                return@forEachIndexed
            }
            require(result.state != RootScriptItemState.Dispatching && result.state != RootScriptItemState.NotRun) {
                "unfinished Finish item"
            }
            if (!requiredPayloadSucceeded && !item.alwaysRun) {
                require(result.state == RootScriptItemState.SkippedDependency) { "Finish dependency skip mismatch" }
            } else {
                require(result.state != RootScriptItemState.SkippedDependency) { "always-run Finish was skipped" }
            }
        }
        if (durableHandoff) {
            require(finishResults.count { it.state == RootScriptItemState.Dispatching } == 1) {
                "handoff dispatch count"
            }
        } else {
            require(finishResults.none { it.state == RootScriptItemState.Dispatching }) {
                "dispatching Finish without handoff"
            }
        }
    }

    /** @return True for the unique no-exit representation. */
    private fun RootScriptItemResult.hasNoExit(): Boolean =
        exitKind == RootScriptExitKind.None && exitValue == 0

    /** @return True for the requested process exit representation. */
    private fun RootScriptItemResult.hasProcessExit(value: Int): Boolean =
        exitKind == RootScriptExitKind.ProcessExit && exitValue == value

    /** Removes one exact prefix or rejects the line. */
    private fun String.removeExactPrefix(prefix: String): String {
        require(startsWith(prefix)) { "GLR1 field order or identity" }
        return substring(prefix.length)
    }

    /** Parses canonical non-negative decimal syntax with no sign or leading zero. */
    private fun parseNonNegativeDecimal(value: String): Int {
        require(value == "0" || Regex("^[1-9][0-9]*$").matches(value)) { "non-canonical decimal" }
        return value.toIntOrNull() ?: throw IllegalArgumentException("decimal overflow")
    }

    /** Parses canonical positive generation syntax. */
    private fun parsePositiveDecimal(value: String): Long {
        require(Regex("^[1-9][0-9]*$").matches(value)) { "non-canonical generation" }
        return value.toLongOrNull()?.takeIf { it > 0L }
            ?: throw IllegalArgumentException("generation overflow")
    }

    /** Parses the only two handoff values. */
    private fun parseZeroOrOne(value: String): Int = parseNonNegativeDecimal(value).also {
        require(it == 0 || it == 1) { "invalid handoff" }
    }
}
