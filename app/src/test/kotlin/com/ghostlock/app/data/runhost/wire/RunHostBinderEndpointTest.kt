package com.ghostlock.app.data.runhost.wire

import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.BackendRunFacts
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.complete
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostCandidateResult
import com.ghostlock.app.core.runhost.RunHostCode
import com.ghostlock.app.core.runhost.RunHostController
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.runhost.RunHostPreparedCandidate
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.core.runhost.RunHostState
import com.ghostlock.app.core.runhost.RunHostWorkspaceLease
import com.ghostlock.app.data.runhost.host.AndroidRunHostPreparationAdapter
import com.ghostlock.app.data.runhost.host.RunHostRole
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.ItemState
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.PayloadItemResult
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.run.IRunHostCallback
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunHostSnapshot
import com.ghostlock.app.run.RunInputArtifactParcel
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Binder endpoint identity, Parcel budget, callback failure, and bounded reply cases. */
@RunWith(RobolectricTestRunner::class)
class RunHostBinderEndpointTest {
    private val identity = RunHostIdentity("2".repeat(32), 3L)

    @Test
    fun invalidIdentityAndPrePrepareStartMapToStableCodes() {
        val endpoint = endpoint(RunHostController())
        assertEquals(
            RunHostCode.INVALID_REQUEST.wireValue,
            endpoint.setCallback("bad", 0L, callback()),
        )
        assertEquals(
            RunHostCode.INVALID_REQUEST.wireValue,
            endpoint.getSnapshot("bad", 0L).code,
        )
        assertEquals(
            RunHostCode.BAD_STATE.wireValue,
            endpoint.start(identity.runId, identity.generation),
        )
    }

    @Test
    fun actualRequestParcelBudgetRejectsBeforeModelOrPreparation() {
        val request = RunHostRequest().also { wire ->
            wire.runId = identity.runId
            wire.generation = identity.generation
            wire.backendKey = "x".repeat(300_000)
            wire.profileArtifactId = "profile"
            wire.artifacts = emptyList()
            wire.payloadItems = emptyList()
            wire.finishItems = emptyList()
        }
        assertTrue(request.parcelDataSize() > com.ghostlock.app.core.runhost.RunHostProtocol.MAX_REQUEST_PARCEL_BYTES)
        assertEquals(RunHostCode.LIMIT_EXCEEDED.wireValue, endpoint(RunHostController()).prepare(request))
    }

    @Test
    fun malformedGeneratedModelMapsInvalidRequestWithoutDuplicatingDescriptors() {
        val request = RunHostRequest().also { wire ->
            wire.runId = identity.runId
            wire.generation = identity.generation
            wire.backendKey = "diagnose"
            wire.profileArtifactId = "profile"
            wire.artifacts = listOf(
                RunInputArtifactParcel().also { artifact ->
                    artifact.logicalId = "profile"
                    artifact.kind = Int.MAX_VALUE
                    artifact.byteCount = 0L
                    artifact.sha256 = "0".repeat(64)
                    artifact.fd = null
                },
            )
            wire.payloadItems = emptyList()
            wire.finishItems = emptyList()
        }
        assertEquals(RunHostCode.INVALID_REQUEST.wireValue, endpoint(RunHostController()).prepare(request))
    }

    @Test
    fun callbackFailureDropsLogButTerminalSnapshotRemainsBoundedAndAuthoritative() {
        val controller = RunHostController()
        val endpoint = endpoint(controller)
        assertEquals(RunHostCode.OK.wireValue, endpoint.setCallback(identity.runId, identity.generation, callback(
            onLog = { throw IllegalStateException("binder callback failed") },
        )))
        assertEquals(
            RunHostCode.OK,
            controller.prepare(requestModel()) { _, _ -> preparedCandidate() },
        )
        assertEquals(RunHostCode.OK.wireValue, endpoint.start(identity.runId, identity.generation))
        waitUntil {
            endpoint.getSnapshot(identity.runId, identity.generation).snapshot?.state == RunHostState.TERMINAL.wireValue
        }

        val reply = endpoint.getSnapshot(identity.runId, identity.generation)
        assertEquals(RunHostCode.OK.wireValue, reply.code)
        val snapshot = requireNotNull(reply.snapshot)
        assertEquals(1L, snapshot.droppedLogs)
        assertTrue(snapshot.parcelDataSize() <= com.ghostlock.app.core.runhost.RunHostProtocol.MAX_SNAPSHOT_PARCEL_BYTES)
    }

    @Test
    fun oversizedActualLogParcelIsDroppedBeforeBinderDelivery() {
        val controller = RunHostController()
        val endpoint = endpoint(controller)
        val delivered = AtomicInteger(0)
        val message = "a".repeat(com.ghostlock.app.core.runhost.RunHostProtocol.MAX_LOG_MESSAGE_BYTES)
        assertEquals(
            RunHostCode.OK.wireValue,
            endpoint.setCallback(
                identity.runId,
                identity.generation,
                callback(onLog = { delivered.incrementAndGet() }),
            ),
        )
        assertEquals(
            RunHostCode.OK,
            controller.prepare(requestModel()) { _, _ ->
                preparedCandidate(message)
            },
        )
        assertEquals(RunHostCode.OK.wireValue, endpoint.start(identity.runId, identity.generation))
        waitUntil {
            endpoint.getSnapshot(identity.runId, identity.generation).snapshot?.state == RunHostState.TERMINAL.wireValue
        }

        val snapshot = requireNotNull(endpoint.getSnapshot(identity.runId, identity.generation).snapshot)
        assertEquals(0, delivered.get())
        assertEquals(1L, snapshot.droppedLogs)
    }

    @Test
    fun boundedSnapshotFallbackContainsTypedUnknownFacts() {
        val controller = RunHostController()
        val endpoint = endpoint(controller)
        val mutablePayload = mutableListOf<PayloadItemResult>()
        assertEquals(
            RunHostCode.OK,
            controller.prepare(requestModel()) { _, _ ->
                preparedCandidate(
                    logMessage = null,
                    reportTransform = { report -> report.copy(payloadItems = mutablePayload) },
                )
            },
        )
        assertEquals(RunHostCode.OK.wireValue, endpoint.start(identity.runId, identity.generation))
        waitUntil {
            controller.snapshot(identity).snapshot?.state == RunHostState.TERMINAL
        }
        repeat(12_000) {
            mutablePayload += PayloadItemResult(
                itemId = "mutated-payload",
                state = ItemState.SUCCESS,
                reason = ResultReason.NONE,
                exitKind = ExitKind.NONE,
                exitValue = 0,
            )
        }

        val reply = endpoint.getSnapshot(identity.runId, identity.generation)
        val snapshot = requireNotNull(reply.snapshot)
        assertEquals(RunHostCode.OK.wireValue, reply.code)
        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE.wireValue, snapshot.lastCode)
        assertEquals(RunTerminal.UNKNOWN.wireValue, snapshot.terminal)
        assertTrue(snapshot.payloadItems.isEmpty())
        assertTrue(snapshot.parcelDataSize() <= com.ghostlock.app.core.runhost.RunHostProtocol.MAX_SNAPSHOT_PARCEL_BYTES)
    }

    @Test
    fun terminalCallbackFailureDoesNotChangeDurableSnapshot() {
        val controller = RunHostController()
        val endpoint = endpoint(controller)
        val terminalCalls = AtomicInteger(0)
        assertEquals(
            RunHostCode.OK.wireValue,
            endpoint.setCallback(
                identity.runId,
                identity.generation,
                callback(onTerminal = {
                    terminalCalls.incrementAndGet()
                    throw IllegalStateException("terminal callback failed")
                }),
            ),
        )
        assertEquals(
            RunHostCode.OK,
            controller.prepare(requestModel()) { _, _ -> preparedCandidate(logMessage = null) },
        )
        assertEquals(RunHostCode.OK.wireValue, endpoint.start(identity.runId, identity.generation))
        waitUntil {
            terminalCalls.get() == 1
        }

        val reply = endpoint.getSnapshot(identity.runId, identity.generation)
        assertEquals(1, terminalCalls.get())
        assertEquals(RunHostCode.OK.wireValue, reply.code)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE.wireValue, requireNotNull(reply.snapshot).terminal)
    }

    private fun endpoint(controller: RunHostController): RunHostBinderEndpoint = RunHostBinderEndpoint(
        controller = controller,
        preparation = AndroidRunHostPreparationAdapter(
            role = RunHostRole.APP,
            hostEnvironment = { null },
            resolveRuntime = { null },
            processMatches = { _, _, _ -> true },
        ),
    )

    private fun callback(
        onLog: (RunHostLogRecord) -> Unit = {},
        onTerminal: (RunHostSnapshot) -> Unit = {},
    ): IRunHostCallback = object : IRunHostCallback.Stub() {
        override fun onLog(record: RunHostLogRecord) = onLog.invoke(record)

        override fun onTerminal(snapshot: RunHostSnapshot) = onTerminal.invoke(snapshot)
    }

    private fun preparedCandidate(
        logMessage: String? = "endpoint-log",
        reportTransform: (RunReport) -> RunReport = { report -> report },
    ): RunHostCandidateResult =
        RunHostCandidateResult.Prepared(
            RunHostPreparedCandidate(
                identity = identity,
                abortPolicy = AbortPolicy.SAFE_DURING_RUN,
                flow = object : Flow {
                    override suspend fun run(input: BackendFlowInput): RunReport {
                        if (logMessage != null) {
                            input.logs.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, logMessage))
                        }
                        return reportTransform(
                            input.complete(
                                BackendRunFacts(
                                    primitive = StageResult.SUCCESS,
                                    scriptDispatch = StageResult.SUCCESS,
                                    cleanup = StageResult.SUCCESS,
                                    terminalHint = BackendTerminalHint.NONE,
                                    diagnosticOutcome = DiagnosticOutcome.PASS,
                                ),
                            ),
                        )
                    }

                    override fun stop() = Unit
                },
                workspace = object : RunHostWorkspaceLease {
                    override fun activateExecutionEnvironment() = Unit

                    override fun deleteAfterTerminal(): Boolean = true
                },
                rootScript = rootScript(),
                profile = ProfileSnapshot("profile", ProfileSnapshot.CURRENT_SCHEMA_VERSION, "0".repeat(64)),
                artifacts = emptyMap(),
                persistentStateDirectory = "/state",
                reportContext = FlowReportContext(RunCapability.DIAGNOSTIC_ONLY, null, null),
            ),
        )

    private fun requestModel(): RunHostRequestModel = RunHostRequestModel(
        runId = identity.runId,
        generation = identity.generation,
        backendKey = "diagnose",
        profileArtifactId = "profile",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = listOf(
            RunHostArtifactRequest("profile", RunHostArtifactKind.PROFILE, 2L, "0".repeat(64)),
        ),
    )

    private fun rootScript(): RootScriptArtifact {
        val published = PublishedArtifactIdentity(1uL, 1uL, 0L, "0".repeat(64), 0u)
        return RootScriptArtifact(
            runId = identity.runId,
            generation = identity.generation,
            scriptPath = "/run/root-script.sh",
            resultPath = "/run/result",
            scriptIdentity = published,
            resultIdentity = published,
            retainedArtifacts = emptyList(),
        )
    }

    private fun waitUntil(predicate: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5L)
        while (!predicate()) {
            check(System.nanoTime() < deadline) { "endpoint result did not become terminal" }
            Thread.sleep(10L)
        }
    }
}
