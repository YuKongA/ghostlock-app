package com.ghostlock.app.core.rootscript

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Strict GLR1 grammar, same-plan identity, ordering, and policy checks. */
class RootScriptResultCodecTest {
    private val runId = "0123456789abcdef0123456789abcdef"
    private val plan = RootScriptExecutionPlan(
        runId = runId,
        generation = 1L,
        payload = listOf(command("payload-root")),
        finish = emptyList(),
    )

    @Test
    fun decodesCompleteOrderedResult() {
        val result = RootScriptResultCodec.decode(validResult().toByteArray(Charsets.US_ASCII), plan)
        assertEquals(RootScriptItemState.Success, result.payloadItems.single().state)
        assertEquals(false, result.durableHandoff)
    }

    @Test
    fun rejectsMissingCompleteUnknownItemAndDuplicatePrefix() {
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(validResult().replace("complete=1\n", "").toByteArray(), plan)
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(validResult().replace("payload-root", "unknown").toByteArray(), plan)
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                validResult().replace("payload.payload-root.reason=0", "payload.payload-root.state=2").toByteArray(),
                plan,
            )
        }
    }

    @Test
    fun rejectsNonCanonicalDecimalAndPartialFile() {
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(validResult().replace("generation=1", "generation=01").toByteArray(), plan)
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(validResult().dropLast(1).toByteArray(), plan)
        }
    }

    @Test
    fun rejectsOutOfOrderRecordsAndUnknownWireValues() {
        val orderedPlan = plan.copy(payload = listOf(command("payload-first"), command("payload-second")))
        val first = itemRecord("payload", "payload-first", 2, 0, 1, 0)
        val second = itemRecord("payload", "payload-second", 2, 0, 1, 0)
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(completeResult(listOf(second, first)).toByteArray(), orderedPlan)
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(validResult().replace("state=2", "state=9").toByteArray(), plan)
        }
    }

    @Test
    fun mixedKindsKeepExactPlanOrderAcceptanceAndArgvTokens() {
        val mixed = plan.copy(
            payload = listOf(
                command("payload-command", args = listOf("echo", "two words")),
                RootScriptPayloadExecution(
                    "payload-script",
                    RootScriptPayloadKind.SCRIPT,
                    false,
                    "/stage/script",
                    listOf("two words"),
                    RootScriptPayloadAcceptance.EXIT_ZERO,
                ),
                RootScriptPayloadExecution(
                    "payload-manager",
                    RootScriptPayloadKind.ROOT_MANAGER,
                    false,
                    "/stage/manager",
                    listOf("late-load", "--kmi", "android13-5.15"),
                    RootScriptPayloadAcceptance.DAEMON_ACK,
                ),
                RootScriptPayloadExecution(
                    "payload-lkm",
                    RootScriptPayloadKind.LKM,
                    false,
                    "/stage/module.ko",
                    listOf("mode=1"),
                    RootScriptPayloadAcceptance.MODULE_PRESENT,
                ),
                RootScriptPayloadExecution(
                    "payload-detached",
                    RootScriptPayloadKind.DETACHED_PROCESS,
                    false,
                    "/stage/daemon",
                    listOf("--label", "two words"),
                    RootScriptPayloadAcceptance.TAKEOVER_ACK,
                ),
            ),
        )
        val records = mixed.payload.map { item -> itemRecord("payload", item.itemId, 2, 0, 1, 0) }
        val result = RootScriptResultCodec.decode(completeResult(records).toByteArray(), mixed)
        assertEquals(mixed.payload.map(RootScriptPayloadExecution::itemId), result.payloadItems.map { it.itemId })
        assertEquals(RootScriptPayloadAcceptance.TAKEOVER_ACK, mixed.payload.last().acceptance)
        assertEquals(listOf("--label", "two words"), mixed.payload.last().args)
    }

    @Test
    fun enforcesRequiredFailFastAndFinishAlwaysRunPolicy() {
        val policyPlan = plan.copy(
            payload = listOf(command("payload-first"), command("payload-second")),
            finish = listOf(
                finishCommand("finish-dependent", alwaysRun = false),
                finishCommand("finish-always", alwaysRun = true, safety = true),
            ),
        )
        val firstFailed = itemRecord("payload", "payload-first", 3, 5, 1, 1)
        val secondSkipped = itemRecord("payload", "payload-second", 4, 1, 0, 0)
        val dependentSkipped = itemRecord("finish", "finish-dependent", 4, 1, 0, 0)
        val alwaysSucceeded = itemRecord("finish", "finish-always", 2, 0, 1, 0)
        val valid = completeResult(
            payloadRecords = listOf(firstFailed, secondSkipped),
            finishRecords = listOf(dependentSkipped, alwaysSucceeded),
        )
        assertEquals(
            RootScriptItemState.Failed,
            RootScriptResultCodec.decode(valid.toByteArray(), policyPlan).payloadItems.first().state,
        )
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                completeResult(
                    listOf(firstFailed, itemRecord("payload", "payload-second", 2, 0, 1, 0)),
                    listOf(dependentSkipped, alwaysSucceeded),
                ).toByteArray(),
                policyPlan,
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                completeResult(
                    listOf(firstFailed, secondSkipped),
                    listOf(itemRecord("finish", "finish-dependent", 2, 0, 1, 0), alwaysSucceeded),
                ).toByteArray(),
                policyPlan,
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                completeResult(
                    listOf(firstFailed, secondSkipped),
                    listOf(dependentSkipped, itemRecord("finish", "finish-always", 4, 1, 0, 0)),
                ).toByteArray(),
                policyPlan,
            )
        }
    }

    @Test
    fun rebootRequiresDurableHandoffAndDispatchRecord() {
        val rebootPlan = plan.copy(
            finish = listOf(
                RootScriptFinishExecution(
                    "finish-reboot",
                    RootScriptFinishKind.REBOOT,
                    RootScriptFinishSeverity.SAFETY_CRITICAL,
                    true,
                    null,
                    emptyList(),
                ),
            ),
        )
        val payload = itemRecord("payload", "payload-root", 2, 0, 1, 0)
        val dispatch = itemRecord("finish", "finish-reboot", 1, 0, 0, 0)
        val decoded = RootScriptResultCodec.decode(
            completeResult(listOf(payload), listOf(dispatch), handoff = 1).toByteArray(),
            rebootPlan,
        )
        assertTrue(decoded.durableHandoff)
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                completeResult(listOf(payload), listOf(dispatch), handoff = 0).toByteArray(),
                rebootPlan,
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                completeResult(
                    listOf(payload),
                    listOf(itemRecord("finish", "finish-reboot", 2, 0, 1, 0)),
                    handoff = 0,
                ).toByteArray(),
                rebootPlan,
            )
        }
    }

    @Test
    fun rejectsReservedReasonInvalidExitConjunctionAndOversizeFile() {
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(validResult().replace("reason=0", "reason=12").toByteArray(), plan)
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(
                validResult().replace("state=2", "state=3").replace("reason=0", "reason=5").toByteArray(),
                plan,
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            RootScriptResultCodec.decode(ByteArray(256 * 1024 + 1) { 'a'.code.toByte() }, plan)
        }
    }

    private fun validResult(): String = completeResult(
        listOf(itemRecord("payload", "payload-root", 2, 0, 1, 0)),
    )

    private fun command(
        itemId: String,
        args: List<String> = listOf("id"),
        required: Boolean = true,
    ): RootScriptPayloadExecution = RootScriptPayloadExecution(
        itemId,
        RootScriptPayloadKind.COMMAND,
        required,
        null,
        args,
        RootScriptPayloadAcceptance.EXIT_ZERO,
    )

    private fun finishCommand(
        itemId: String,
        alwaysRun: Boolean,
        safety: Boolean = false,
    ): RootScriptFinishExecution = RootScriptFinishExecution(
        itemId,
        RootScriptFinishKind.COMMAND,
        if (safety) RootScriptFinishSeverity.SAFETY_CRITICAL else RootScriptFinishSeverity.NORMAL,
        alwaysRun,
        null,
        listOf("/system/bin/id"),
    )

    private fun itemRecord(
        phase: String,
        itemId: String,
        state: Int,
        reason: Int,
        exitKind: Int,
        exitValue: Int,
    ): String = buildString {
        appendLine("$phase.$itemId.state=$state")
        appendLine("$phase.$itemId.reason=$reason")
        appendLine("$phase.$itemId.exit_kind=$exitKind")
        appendLine("$phase.$itemId.exit_value=$exitValue")
    }

    private fun completeResult(
        payloadRecords: List<String>,
        finishRecords: List<String> = emptyList(),
        handoff: Int = 0,
    ): String = buildString {
        appendLine("GLR1")
        appendLine("run=$runId")
        appendLine("generation=1")
        payloadRecords.forEach(::append)
        finishRecords.forEach(::append)
        appendLine("handoff=$handoff")
        appendLine("complete=1")
    }
}
