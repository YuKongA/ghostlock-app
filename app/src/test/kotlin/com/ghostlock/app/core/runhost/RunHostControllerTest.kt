package com.ghostlock.app.core.runhost

import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.BackendRunFacts
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.complete
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.run.BackendTerminalHint
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunReport
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Shared controller ownership, terminal, callback, and quiescence cases. */
class RunHostControllerTest {
    private val identity = RunHostIdentity("0".repeat(32), 1L)

    @Test
    fun prepareStartTerminalAndCloseUseOneCandidate() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace()
        val terminal = CountDownLatch(1)
        val logs = Collections.synchronizedList(mutableListOf<LogDraft>())
        val sink = RecordingSink(logs = logs, terminal = terminal)

        assertEquals(RunHostCode.OK, controller.setCallback(identity, sink))
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(workspace, successfulFlow(emitLog = true)) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))

        val snapshot = controller.snapshot(identity).snapshot
        assertEquals(RunHostState.TERMINAL, snapshot?.state)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, snapshot?.terminal)
        assertEquals(1L, snapshot?.logSequence)
        assertEquals(1, workspace.activations.get())
        assertEquals(listOf("controller-log"), logs.map(LogDraft::message))
        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertEquals(1, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
        assertEquals(RunHostCode.ALREADY_APPLIED, controller.requestClose(identity))
    }

    @Test
    fun materializeFailureDoesNotCommitCandidate() {
        val controller = RunHostController()
        assertEquals(RunHostCode.OK, controller.setCallback(identity, RecordingSink()))
        assertEquals(
            RunHostCode.NOT_READY,
            controller.prepare(request()) { _, _ -> RunHostCandidateResult.Rejected(RunHostCode.NOT_READY) },
        )
        assertEquals(RunHostState.NEW, controller.snapshot(identity).snapshot?.state)
        assertEquals(RunHostCode.BAD_STATE, controller.start(identity))
    }

    @Test
    fun preparedCancelPublishesCancelledThenCloseDeletesTheWorkspace() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace()
        val terminal = CountDownLatch(1)
        val stopCalls = AtomicInteger(0)
        val flow = RecordingFlow(
            runAction = { error("cancelled candidate must not start") },
            stopAction = { stopCalls.incrementAndGet() },
        )
        controller.setCallback(identity, RecordingSink(terminal = terminal))
        assertEquals(RunHostCode.OK, controller.prepare(request()) { _, _ -> prepared(workspace, flow) })

        assertEquals(RunHostCode.OK, controller.cancel(identity))
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))
        val snapshot = controller.snapshot(identity).snapshot
        assertEquals(RunHostState.TERMINAL, snapshot?.state)
        assertEquals(RunTerminal.CANCELLED, snapshot?.terminal)
        assertEquals(0, stopCalls.get())

        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertEquals(1, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun candidateIdentityMismatchCleansWorkspaceAndLeavesControllerQuiescent() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace { error("cleanup residue") }
        controller.setCallback(identity, RecordingSink())
        val wrongIdentity = RunHostIdentity("f".repeat(32), identity.generation)
        val result = controller.prepare(request()) { _, _ ->
            val candidate = (prepared(workspace, successfulFlow()) as RunHostCandidateResult.Prepared).candidate
            RunHostCandidateResult.Prepared(candidate.copy(identity = wrongIdentity))
        }
        assertEquals(RunHostCode.INTERNAL_CONTRACT_FAILURE, result)
        assertEquals(1, workspace.deletions.get())
        assertEquals(RunHostState.NEW, controller.snapshot(identity).snapshot?.state)
        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun concurrentPrepareCleansExactlyOneLosingWorkspace() {
        val controller = RunHostController()
        val first = RecordingWorkspace()
        val second = RecordingWorkspace()
        val materialized = CountDownLatch(2)
        val release = CountDownLatch(1)
        val codes = Collections.synchronizedList(mutableListOf<RunHostCode>())
        val index = AtomicInteger(0)
        val threads = List(2) {
            Thread {
                val code = controller.prepare(request()) { _, _ ->
                    val workspace = if (index.getAndIncrement() == 0) first else second
                    materialized.countDown()
                    assertTrue(release.await(5L, TimeUnit.SECONDS))
                    prepared(workspace, successfulFlow())
                }
                codes += code
            }
        }
        threads.forEach(Thread::start)
        assertTrue(materialized.await(5L, TimeUnit.SECONDS))
        release.countDown()
        threads.forEach { thread -> thread.join(5_000L) }

        assertEquals(setOf(RunHostCode.OK, RunHostCode.ALREADY_APPLIED), codes.toSet())
        assertEquals(1, first.deletions.get() + second.deletions.get())
    }

    @Test
    fun cancelAndCloseWaitForBlockedMaterializerAndItsLosingWorkspaceCleanup() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace()
        val materializerEntered = CountDownLatch(1)
        val releaseMaterializer = CountDownLatch(1)
        val terminal = CountDownLatch(1)
        val prepareCode = AtomicReference<RunHostCode>()
        val cancelCode = AtomicReference<RunHostCode>()
        val closeCode = AtomicReference<RunHostCode>()
        controller.setCallback(identity, RecordingSink(terminal = terminal))

        val prepareThread = Thread {
            prepareCode.set(controller.prepare(request()) { _, _ ->
                materializerEntered.countDown()
                assertTrue(releaseMaterializer.await(5L, TimeUnit.SECONDS))
                prepared(workspace, successfulFlow())
            })
        }
        prepareThread.start()
        assertTrue(materializerEntered.await(5L, TimeUnit.SECONDS))

        val cancelThread = Thread { cancelCode.set(controller.cancel(identity)) }
        cancelThread.start()
        waitUntil { controller.snapshot(identity).snapshot?.state == RunHostState.TERMINAL }
        val closeThread = Thread { closeCode.set(controller.requestClose(identity)) }
        closeThread.start()
        assertFalse(terminal.await(100L, TimeUnit.MILLISECONDS))
        assertTrue(cancelThread.isAlive)
        assertTrue(closeThread.isAlive)
        assertFalse(controller.frameworkDestroyAuthorized())

        releaseMaterializer.countDown()
        prepareThread.join(5_000L)
        cancelThread.join(5_000L)
        closeThread.join(5_000L)

        assertFalse(prepareThread.isAlive)
        assertFalse(cancelThread.isAlive)
        assertFalse(closeThread.isAlive)
        assertEquals(RunHostCode.BAD_STATE, prepareCode.get())
        assertEquals(RunHostCode.OK, cancelCode.get())
        assertEquals(RunHostCode.OK, closeCode.get())
        assertEquals(1, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun runningCloseReturnsImmediatelyWithoutOvertakingWorker() {
        val controller = RunHostController()
        val flowEntered = CountDownLatch(1)
        val releaseFlow = CountDownLatch(1)
        val terminal = CountDownLatch(1)
        val flow = RecordingFlow(runAction = { input ->
            flowEntered.countDown()
            assertTrue(releaseFlow.await(5L, TimeUnit.SECONDS))
            input.complete(successfulFacts())
        })
        assertEquals(RunHostCode.OK, controller.setCallback(identity, RecordingSink(terminal = terminal)))
        assertEquals(RunHostCode.OK, controller.prepare(request()) { _, _ -> prepared(RecordingWorkspace(), flow) })
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(flowEntered.await(5L, TimeUnit.SECONDS))

        assertEquals(RunHostCode.UNSAFE_TO_DESTROY, controller.requestClose(identity))
        assertFalse(controller.frameworkDestroyAuthorized())
        releaseFlow.countDown()
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))
    }

    @Test
    fun unsafeRunningCancelDoesNotStopOrChangeTheWorker() {
        val controller = RunHostController()
        val flowEntered = CountDownLatch(1)
        val releaseFlow = CountDownLatch(1)
        val terminal = CountDownLatch(1)
        val stopCalls = AtomicInteger(0)
        val flow = RecordingFlow(
            runAction = { input ->
                flowEntered.countDown()
                assertTrue(releaseFlow.await(5L, TimeUnit.SECONDS))
                input.complete(successfulFacts())
            },
            stopAction = { stopCalls.incrementAndGet() },
        )
        controller.setCallback(identity, RecordingSink(terminal = terminal))
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ ->
                prepared(RecordingWorkspace(), flow, AbortPolicy.BEFORE_START_ONLY)
            },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(flowEntered.await(5L, TimeUnit.SECONDS))

        assertEquals(RunHostCode.UNSAFE_TO_CANCEL, controller.cancel(identity))
        assertEquals(0, stopCalls.get())
        assertEquals(RunHostState.RUNNING, controller.snapshot(identity).snapshot?.state)

        releaseFlow.countDown()
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, controller.snapshot(identity).snapshot?.terminal)
    }

    @Test
    fun terminalCallbackWaitsUntilTheFinalLogDeliveryHasDrained() {
        val controller = RunHostController()
        val logEntered = CountDownLatch(1)
        val releaseLog = CountDownLatch(1)
        val terminal = CountDownLatch(1)
        val sink = object : RunHostEventSink {
            override fun publishLog(
                identity: RunHostIdentity,
                sequence: Long,
                draft: LogDraft,
            ): Boolean {
                logEntered.countDown()
                assertTrue(releaseLog.await(5L, TimeUnit.SECONDS))
                return true
            }

            override fun publishTerminal(snapshot: RunHostSnapshotModel): Boolean {
                terminal.countDown()
                return true
            }
        }
        controller.setCallback(identity, sink)
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ ->
                prepared(RecordingWorkspace(), successfulFlow(emitLog = true))
            },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(logEntered.await(5L, TimeUnit.SECONDS))
        assertFalse(terminal.await(100L, TimeUnit.MILLISECONDS))

        releaseLog.countDown()
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))
        assertEquals(1L, controller.snapshot(identity).snapshot?.logSequence)
    }

    @Test
    fun closeWaitsAfterWorkerClaimsTerminalUntilReportAndDrainFinish() {
        val controller = RunHostController()
        val terminalClaimed = CountDownLatch(1)
        val releaseReport = CountDownLatch(1)
        val terminal = CountDownLatch(1)
        val workspace = RecordingWorkspace()
        val flow = RecordingFlow(runAction = { input ->
            val report = input.complete(successfulFacts())
            terminalClaimed.countDown()
            assertTrue(releaseReport.await(5L, TimeUnit.SECONDS))
            report
        })
        controller.setCallback(identity, RecordingSink(terminal = terminal))
        assertEquals(RunHostCode.OK, controller.prepare(request()) { _, _ -> prepared(workspace, flow) })
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(terminalClaimed.await(5L, TimeUnit.SECONDS))

        val closeCode = AtomicReference<RunHostCode>()
        val closeThread = Thread { closeCode.set(controller.requestClose(identity)) }
        closeThread.start()
        assertFalse(terminal.await(100L, TimeUnit.MILLISECONDS))
        assertTrue(closeThread.isAlive)
        assertFalse(controller.frameworkDestroyAuthorized())

        releaseReport.countDown()
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))
        closeThread.join(5_000L)
        assertFalse(closeThread.isAlive)
        assertEquals(RunHostCode.OK, closeCode.get())
        assertEquals(1, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun closeWaitsForSafeStopEvenAfterWorkerPublishesTerminal() {
        val controller = RunHostController()
        val flowEntered = CountDownLatch(1)
        val releaseFlow = CountDownLatch(1)
        val stopEntered = CountDownLatch(1)
        val releaseStop = CountDownLatch(1)
        val terminal = CountDownLatch(1)
        val flow = RecordingFlow(
            runAction = { input ->
                flowEntered.countDown()
                assertTrue(releaseFlow.await(5L, TimeUnit.SECONDS))
                input.complete(successfulFacts())
            },
            stopAction = {
                stopEntered.countDown()
                assertTrue(releaseStop.await(5L, TimeUnit.SECONDS))
            },
        )
        val workspace = RecordingWorkspace()
        assertEquals(RunHostCode.OK, controller.setCallback(identity, RecordingSink(terminal = terminal)))
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(workspace, flow, AbortPolicy.SAFE_DURING_RUN) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(flowEntered.await(5L, TimeUnit.SECONDS))

        val cancelCode = AtomicReference<RunHostCode>()
        val cancelThread = Thread { cancelCode.set(controller.cancel(identity)) }
        cancelThread.start()
        assertTrue(stopEntered.await(5L, TimeUnit.SECONDS))
        releaseFlow.countDown()
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))

        val closeCode = AtomicReference<RunHostCode>()
        val closeThread = Thread { closeCode.set(controller.requestClose(identity)) }
        closeThread.start()
        Thread.sleep(100L)
        assertTrue(closeThread.isAlive)
        assertFalse(controller.frameworkDestroyAuthorized())
        releaseStop.countDown()
        cancelThread.join(5_000L)
        closeThread.join(5_000L)

        assertEquals(RunHostCode.OK, cancelCode.get())
        assertEquals(RunHostCode.OK, closeCode.get())
        assertEquals(1, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun repeatedCloseWaitsForTheSameBlockingWorkspaceDeletion() {
        val deleteEntered = CountDownLatch(1)
        val releaseDelete = CountDownLatch(1)
        val workspace = RecordingWorkspace {
            deleteEntered.countDown()
            assertTrue(releaseDelete.await(5L, TimeUnit.SECONDS))
        }
        val controller = RunHostController()
        controller.setCallback(identity, RecordingSink())
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(workspace, successfulFlow()) },
        )
        val firstCode = AtomicReference<RunHostCode>()
        val secondCode = AtomicReference<RunHostCode>()
        val first = Thread { firstCode.set(controller.requestClose(identity)) }
        val second = Thread { secondCode.set(controller.requestClose(identity)) }
        first.start()
        assertTrue(deleteEntered.await(5L, TimeUnit.SECONDS))
        second.start()
        Thread.sleep(100L)
        assertTrue(first.isAlive)
        assertTrue(second.isAlive)
        assertFalse(controller.frameworkDestroyAuthorized())

        releaseDelete.countDown()
        first.join(5_000L)
        second.join(5_000L)
        assertFalse(first.isAlive)
        assertFalse(second.isAlive)
        assertEquals(setOf(RunHostCode.OK, RunHostCode.ALREADY_APPLIED), setOf(firstCode.get(), secondCode.get()))
        assertEquals(1, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun retainingTerminalReleasesCandidateWithoutDeletingWorkspace() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace()
        val terminal = CountDownLatch(1)
        val flow = RecordingFlow(runAction = { input ->
            input.complete(successfulFacts().copy(diagnosticOutcome = DiagnosticOutcome.UNKNOWN))
        })
        controller.setCallback(identity, RecordingSink(terminal = terminal))
        assertEquals(RunHostCode.OK, controller.prepare(request()) { _, _ -> prepared(workspace, flow) })
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))
        assertEquals(RunTerminal.UNKNOWN, controller.snapshot(identity).snapshot?.terminal)

        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertEquals(0, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun failedLogDeliveryIsCountedAndDoesNotChangeTerminalResult() {
        val controller = RunHostController()
        val terminal = CountDownLatch(1)
        val sink = RecordingSink(terminal = terminal, acceptLogs = false)
        controller.setCallback(identity, sink)
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(RecordingWorkspace(), successfulFlow(emitLog = true)) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        waitUntil { controller.snapshot(identity).snapshot?.state == RunHostState.TERMINAL }

        val snapshot = controller.snapshot(identity).snapshot
        assertEquals(1L, snapshot?.droppedLogs)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, snapshot?.terminal)
    }

    @Test
    fun flowExceptionStillPublishesOneTypedTerminalAndReleasesWorker() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace()
        val terminal = CountDownLatch(1)
        val terminalCalls = AtomicInteger(0)
        controller.setCallback(identity, object : RunHostEventSink {
            override fun publishLog(identity: RunHostIdentity, sequence: Long, draft: LogDraft): Boolean = true

            override fun publishTerminal(snapshot: RunHostSnapshotModel): Boolean {
                terminalCalls.incrementAndGet()
                terminal.countDown()
                return true
            }
        })
        val flow = RecordingFlow(runAction = { error("backend failed") })
        assertEquals(RunHostCode.OK, controller.prepare(request()) { _, _ -> prepared(workspace, flow) })
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))

        val snapshot = controller.snapshot(identity).snapshot
        assertEquals(RunHostState.TERMINAL, snapshot?.state)
        assertEquals(RunTerminal.UNKNOWN, snapshot?.terminal)
        assertEquals(1, terminalCalls.get())
        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertEquals(0, workspace.deletions.get())
    }

    @Test
    fun invalidClaimedReportFallsBackAndStillReachesQuiescence() {
        val controller = RunHostController()
        val workspace = RecordingWorkspace()
        val terminal = CountDownLatch(1)
        val terminalCalls = AtomicInteger(0)
        controller.setCallback(identity, object : RunHostEventSink {
            override fun publishLog(identity: RunHostIdentity, sequence: Long, draft: LogDraft): Boolean = true

            override fun publishTerminal(snapshot: RunHostSnapshotModel): Boolean {
                terminalCalls.incrementAndGet()
                terminal.countDown()
                return true
            }
        })
        val flow = RecordingFlow(runAction = { input ->
            input.complete(successfulFacts()).copy(terminal = RunTerminal.SUCCESS)
        })
        assertEquals(RunHostCode.OK, controller.prepare(request()) { _, _ -> prepared(workspace, flow) })
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))

        val snapshot = controller.snapshot(identity).snapshot
        assertEquals(RunHostState.TERMINAL, snapshot?.state)
        assertEquals(DiagnosticOutcome.UNKNOWN, snapshot?.diagnosticOutcome)
        assertEquals(RunTerminal.UNKNOWN, snapshot?.terminal)
        assertEquals(1, terminalCalls.get())
        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertEquals(0, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    @Test
    fun replacingCallbackRoutesLogsAndTerminalOnlyToLatestSink() {
        val controller = RunHostController()
        val firstLogs = Collections.synchronizedList(mutableListOf<LogDraft>())
        val secondLogs = Collections.synchronizedList(mutableListOf<LogDraft>())
        val firstTerminal = CountDownLatch(1)
        val secondTerminal = CountDownLatch(1)
        assertEquals(RunHostCode.OK, controller.setCallback(identity, RecordingSink(firstLogs, firstTerminal)))
        assertEquals(RunHostCode.OK, controller.setCallback(identity, RecordingSink(secondLogs, secondTerminal)))
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(RecordingWorkspace(), successfulFlow(emitLog = true)) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(secondTerminal.await(5L, TimeUnit.SECONDS))
        assertFalse(firstTerminal.await(100L, TimeUnit.MILLISECONDS))
        assertTrue(firstLogs.isEmpty())
        assertEquals(listOf("controller-log"), secondLogs.map(LogDraft::message))
    }

    @Test
    fun missingCallbackCountsDroppedLogWithoutChangingTerminalFacts() {
        val controller = RunHostController()
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(RecordingWorkspace(), successfulFlow(emitLog = true)) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        waitUntil { controller.snapshot(identity).snapshot?.state == RunHostState.TERMINAL }
        val snapshot = controller.snapshot(identity).snapshot
        assertEquals(1L, snapshot?.droppedLogs)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, snapshot?.terminal)
    }

    @Test
    fun invalidLogDraftIsDroppedBeforeDelivery() {
        val controller = RunHostController()
        val terminal = CountDownLatch(1)
        val logs = Collections.synchronizedList(mutableListOf<LogDraft>())
        controller.setCallback(identity, RecordingSink(logs = logs, terminal = terminal))
        val flow = RecordingFlow(runAction = { input ->
            input.logs.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, "invalid\nline"))
            input.complete(successfulFacts())
        })
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(RecordingWorkspace(), flow) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        assertTrue(terminal.await(5L, TimeUnit.SECONDS))

        val snapshot = controller.snapshot(identity).snapshot
        assertTrue(logs.isEmpty())
        assertEquals(0L, snapshot?.logSequence)
        assertEquals(1L, snapshot?.droppedLogs)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, snapshot?.terminal)
    }

    @Test
    fun cancelledCoroutineScopeMapsLaunchToTerminalFailureWithoutStrandingQuiescence() {
        val job = SupervisorJob().also { it.cancel() }
        val controller = RunHostController(CoroutineScope(job + Dispatchers.Default))
        val workspace = RecordingWorkspace()
        controller.setCallback(identity, RecordingSink())
        assertEquals(
            RunHostCode.OK,
            controller.prepare(request()) { _, _ -> prepared(workspace, successfulFlow()) },
        )
        assertEquals(RunHostCode.OK, controller.start(identity))
        waitUntil { controller.snapshot(identity).snapshot?.state == RunHostState.TERMINAL }
        assertEquals(RunTerminal.UNKNOWN, controller.snapshot(identity).snapshot?.terminal)
        assertEquals(RunHostCode.OK, controller.requestClose(identity))
        assertEquals(0, workspace.deletions.get())
        assertTrue(controller.frameworkDestroyAuthorized())
    }

    private fun prepared(
        workspace: RecordingWorkspace,
        flow: Flow,
        abortPolicy: AbortPolicy = AbortPolicy.BEFORE_START_ONLY,
    ): RunHostCandidateResult = RunHostCandidateResult.Prepared(
        RunHostPreparedCandidate(
            identity = identity,
            abortPolicy = abortPolicy,
            flow = flow,
            workspace = workspace,
            rootScript = rootScript(),
            profile = ProfileSnapshot("profile", ProfileSnapshot.CURRENT_SCHEMA_VERSION, "0".repeat(64)),
            artifacts = emptyMap(),
            persistentStateDirectory = "/state",
            reportContext = FlowReportContext(RunCapability.DIAGNOSTIC_ONLY, null, null),
        ),
    )

    private fun successfulFlow(emitLog: Boolean = false): Flow = RecordingFlow(runAction = { input ->
        if (emitLog) input.logs.emit(LogDraft(LogOrigin.Kotlin, LogLevel.Info, "controller-log"))
        input.complete(successfulFacts())
    })

    private fun successfulFacts(): BackendRunFacts = BackendRunFacts(
        primitive = StageResult.SUCCESS,
        scriptDispatch = StageResult.SUCCESS,
        cleanup = StageResult.SUCCESS,
        terminalHint = BackendTerminalHint.NONE,
        diagnosticOutcome = DiagnosticOutcome.PASS,
    )

    private fun request(): RunHostRequestModel = RunHostRequestModel(
        runId = identity.runId,
        generation = identity.generation,
        backendKey = "diagnose",
        profileArtifactId = "profile",
        payloadItems = emptyList(),
        finishItems = emptyList(),
        artifacts = listOf(
            RunHostArtifactRequest(
                logicalId = "profile",
                kind = RunHostArtifactKind.PROFILE,
                byteCount = 2L,
                sha256 = "0".repeat(64),
            ),
        ),
    )

    private fun rootScript(): RootScriptArtifact {
        val artifactIdentity = PublishedArtifactIdentity(
            device = 1uL,
            inode = 1uL,
            byteCount = 0L,
            sha256 = "0".repeat(64),
            mode = 0u,
        )
        return RootScriptArtifact(
            runId = identity.runId,
            generation = identity.generation,
            scriptPath = "/run/root-script.sh",
            resultPath = "/run/result",
            scriptIdentity = artifactIdentity,
            resultIdentity = artifactIdentity,
            retainedArtifacts = emptyList(),
        )
    }

    private fun waitUntil(predicate: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5L)
        while (!predicate()) {
            check(System.nanoTime() < deadline) { "condition did not become true" }
            Thread.sleep(10L)
        }
    }

    private class RecordingWorkspace(
        private val deleteAction: () -> Unit = {},
    ) : RunHostWorkspaceLease {
        val activations = AtomicInteger(0)
        val deletions = AtomicInteger(0)

        override fun activateExecutionEnvironment() {
            activations.incrementAndGet()
        }

        override fun deleteAfterTerminal(): Boolean {
            deletions.incrementAndGet()
            deleteAction()
            return true
        }
    }

    private class RecordingSink(
        private val logs: MutableList<LogDraft> = mutableListOf(),
        private val terminal: CountDownLatch = CountDownLatch(0),
        private val acceptLogs: Boolean = true,
    ) : RunHostEventSink {
        override fun publishLog(identity: RunHostIdentity, sequence: Long, draft: LogDraft): Boolean {
            logs += draft
            return acceptLogs
        }

        override fun publishTerminal(snapshot: RunHostSnapshotModel): Boolean {
            terminal.countDown()
            return true
        }
    }

    private class RecordingFlow(
        private val runAction: suspend (BackendFlowInput) -> RunReport,
        private val stopAction: () -> Unit = {},
    ) : Flow {
        override suspend fun run(input: BackendFlowInput): RunReport = runAction(input)

        override fun stop() = stopAction()
    }
}
