package com.ghostlock.app.data.runhost.plugin

import android.os.Process
import com.ghostlock.app.core.flow.BackendFlowInput
import com.ghostlock.app.core.flow.FlowReportContext
import com.ghostlock.app.core.flow.FlowTerminalClaim
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.domain.run.DiagnosticOutcome
import com.ghostlock.app.domain.run.ExitKind
import com.ghostlock.app.domain.run.LogDraft
import com.ghostlock.app.domain.run.LogEmitter
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.ResultReason
import com.ghostlock.app.domain.run.RunCancellationView
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunTerminal
import com.ghostlock.app.domain.run.StageResult
import com.ghostlock.app.domain.run.StageState
import com.ghostlock.plugin.api.v1.DiagnosticBackendRun
import com.ghostlock.plugin.api.v1.DiagnosticRunContext
import com.ghostlock.plugin.api.v1.DiagnosticRunResult
import com.ghostlock.plugin.api.v1.PluginException
import com.ghostlock.plugin.api.v1.PluginExitKind
import com.ghostlock.plugin.api.v1.PluginFailureCode
import com.ghostlock.plugin.api.v1.PluginLogLevel
import com.ghostlock.plugin.api.v1.PluginLogOrigin
import com.ghostlock.plugin.api.v1.PluginResultReason
import com.ghostlock.plugin.api.v1.PluginStageResult
import com.ghostlock.plugin.api.v1.PluginStageState
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Host-side ABI adaptation and one-shot resource ownership for a dynamically loaded backend. */
@RunWith(RobolectricTestRunner::class)
class PluginBackendFlowTest {
    @Test
    fun mapsPluginDtoWithoutChangingIndependentStageFacts() = runBlocking {
        val run = runReturning(
            DiagnosticRunResult(
                PluginStageResult(
                    PluginStageState.FAILED,
                    PluginResultReason.SIGNALLED,
                    PluginExitKind.NATIVE_STATUS,
                    -10,
                ),
                PluginStageResult(
                    PluginStageState.SUCCESS,
                    PluginResultReason.NONE,
                    PluginExitKind.PROCESS_EXIT,
                    0,
                ),
            ),
        )

        val report = fixture(run).flow.run(input())

        assertEquals(
            StageResult(StageState.FAILED, ResultReason.SIGNALLED, ExitKind.NATIVE_STATUS, -10),
            report.backendPrimitive,
        )
        assertEquals(
            StageResult(StageState.SUCCESS, ResultReason.NONE, ExitKind.PROCESS_EXIT, 0),
            report.rootScriptDispatch,
        )
        assertEquals(StageResult.SUCCESS, report.backendCleanup)
        assertEquals(DiagnosticOutcome.FAIL, report.diagnosticOutcome)
        assertEquals(RunTerminal.DIAGNOSTIC_COMPLETE, report.terminal)
    }

    @Test
    fun mapsEveryCheckedPluginFailureToTheFrozenHostFacts() = runBlocking {
        val cases = listOf(
            ExceptionCase(
                PluginFailureCode.INVALID_INPUT,
                StageResult(StageState.FAILED, ResultReason.REJECTED, ExitKind.NONE, 0),
                StageResult.SKIPPED,
                DiagnosticOutcome.FAIL,
            ),
            ExceptionCase(
                PluginFailureCode.NOT_READY,
                StageResult(StageState.FAILED, ResultReason.NOT_READY, ExitKind.NONE, 0),
                StageResult.SKIPPED,
                DiagnosticOutcome.FAIL,
            ),
            ExceptionCase(
                PluginFailureCode.IO_FAILURE,
                StageResult(StageState.FAILED, ResultReason.IO_FAILURE, ExitKind.NONE, 0),
                StageResult.SKIPPED,
                DiagnosticOutcome.FAIL,
            ),
            ExceptionCase(
                PluginFailureCode.CANCELLED,
                StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0),
                StageResult(StageState.CANCELLED, ResultReason.CANCELLED, ExitKind.NONE, 0),
                DiagnosticOutcome.UNKNOWN,
            ),
            ExceptionCase(
                PluginFailureCode.CONTRACT_FAILURE,
                UNKNOWN_STAGE,
                UNKNOWN_STAGE,
                DiagnosticOutcome.UNKNOWN,
            ),
        )

        cases.forEach { case ->
            val report = fixture(
                runThrowing(PluginException(case.code, "fixture failure")),
            ).flow.run(input())

            assertEquals(case.code.name, case.primitive, report.backendPrimitive)
            assertEquals(case.code.name, case.dispatch, report.rootScriptDispatch)
            assertEquals(case.code.name, case.outcome, report.diagnosticOutcome)
            assertEquals(case.code.name, StageResult.SUCCESS, report.backendCleanup)
        }
    }

    @Test
    fun nullAndUndeclaredThrowableBecomeUnknownWithoutSkippingCleanup() = runBlocking {
        val nullCloseCalls = AtomicInteger(0)
        val nullReport = fixture(
            object : DiagnosticBackendRun {
                override fun execute(context: DiagnosticRunContext): DiagnosticRunResult? = null

                override fun close() {
                    nullCloseCalls.incrementAndGet()
                }
            },
        ).flow.run(input())
        val throwableCloseCalls = AtomicInteger(0)
        val throwableReport = fixture(
            object : DiagnosticBackendRun {
                override fun execute(context: DiagnosticRunContext): DiagnosticRunResult =
                    error("undeclared failure")

                override fun close() {
                    throwableCloseCalls.incrementAndGet()
                }
            },
        ).flow.run(input())

        listOf(nullReport, throwableReport).forEach { report ->
            assertEquals(UNKNOWN_STAGE, report.backendPrimitive)
            assertEquals(UNKNOWN_STAGE, report.rootScriptDispatch)
            assertEquals(StageResult.SUCCESS, report.backendCleanup)
            assertEquals(DiagnosticOutcome.UNKNOWN, report.diagnosticOutcome)
            assertEquals(RunTerminal.UNKNOWN, report.terminal)
        }
        assertEquals(1, nullCloseCalls.get())
        assertEquals(1, throwableCloseCalls.get())
    }

    @Test
    fun cleanupFailureHasOutcomePriorityAndCloseRemainsIdempotent() = runBlocking {
        val closeCalls = AtomicInteger(0)
        val fixture = fixture(
            object : DiagnosticBackendRun {
                override fun execute(context: DiagnosticRunContext): DiagnosticRunResult = SUCCESS_RESULT

                override fun close() {
                    closeCalls.incrementAndGet()
                    throw PluginException(PluginFailureCode.IO_FAILURE, "close failed")
                }
            },
        )

        val report = fixture.flow.run(input())
        fixture.owner.close()
        fixture.owner.close()

        assertEquals(StageResult.SUCCESS, report.backendPrimitive)
        assertEquals(
            StageResult(StageState.SUCCESS, ResultReason.NONE, ExitKind.PROCESS_EXIT, 0),
            report.rootScriptDispatch,
        )
        assertEquals(
            StageResult(StageState.FAILED, ResultReason.CLEANUP_INCOMPLETE, ExitKind.NONE, 0),
            report.backendCleanup,
        )
        assertEquals(DiagnosticOutcome.FAIL, report.diagnosticOutcome)
        assertEquals(1, closeCalls.get())
    }

    @Test
    fun contextAndLogsCrossOnlyTheSharedApiAndEveryPluginCallUsesItsLoader() = runBlocking {
        val pluginLoader = object : ClassLoader(javaClass.classLoader) {}
        val closeCalls = AtomicInteger(0)
        val records = mutableListOf<LogDraft>()
        val previousLoader = Thread.currentThread().contextClassLoader
        val run = object : DiagnosticBackendRun {
            override fun execute(context: DiagnosticRunContext): DiagnosticRunResult {
                assertSame(pluginLoader, Thread.currentThread().contextClassLoader)
                assertEquals(RUN_ID, context.runId())
                assertEquals(GENERATION, context.generation())
                assertEquals(Process.myUid(), context.processUid())
                assertEquals(Process.myPid(), context.processPid())
                assertTrue(context.cancellationRequested())
                context.emit(PluginLogOrigin.KOTLIN, PluginLogLevel.DEBUG, "kotlin")
                context.emit(PluginLogOrigin.NATIVE, PluginLogLevel.INFO, "native")
                context.emit(PluginLogOrigin.SHELL, PluginLogLevel.ERROR, "shell")
                return SUCCESS_RESULT
            }

            override fun close() {
                assertSame(pluginLoader, Thread.currentThread().contextClassLoader)
                closeCalls.incrementAndGet()
            }
        }
        val fixture = fixture(run, pluginLoader)

        val report = fixture.flow.run(
            input(
                logs = LogEmitter(records::add),
                cancellationRequested = true,
            ),
        )
        fixture.owner.withPluginContext {
            assertSame(pluginLoader, Thread.currentThread().contextClassLoader)
        }

        assertSame(previousLoader, Thread.currentThread().contextClassLoader)
        assertEquals(
            listOf(
                LogDraft(LogOrigin.Kotlin, LogLevel.Debug, "kotlin"),
                LogDraft(LogOrigin.Native, LogLevel.Info, "native"),
                LogDraft(LogOrigin.Shell, LogLevel.Error, "shell"),
            ),
            records,
        )
        assertEquals(DiagnosticOutcome.PASS, report.diagnosticOutcome)
        assertEquals(1, closeCalls.get())
    }

    @Test
    fun logDeliveryFailureNeverChangesPluginResult() = runBlocking {
        val run = object : DiagnosticBackendRun {
            override fun execute(context: DiagnosticRunContext): DiagnosticRunResult {
                context.emit(PluginLogOrigin.KOTLIN, PluginLogLevel.INFO, "invalid\nline")
                return SUCCESS_RESULT
            }

            override fun close() = Unit
        }

        val report = fixture(run).flow.run(
            input(logs = LogEmitter { error("fixture delivery failure") }),
        )

        assertEquals(StageResult.SUCCESS, report.backendPrimitive)
        assertEquals(DiagnosticOutcome.PASS, report.diagnosticOutcome)
        assertEquals(StageResult.SUCCESS, report.backendCleanup)
    }

    private fun fixture(
        run: DiagnosticBackendRun,
        loader: ClassLoader = object : ClassLoader(javaClass.classLoader) {},
    ): FlowFixture {
        val owner = PluginRunResourceOwner()
        owner.markClassLoading(loader)
        owner.register(run)
        return FlowFixture(PluginBackendFlow(owner), owner)
    }

    private fun runReturning(result: DiagnosticRunResult): DiagnosticBackendRun =
        object : DiagnosticBackendRun {
            override fun execute(context: DiagnosticRunContext): DiagnosticRunResult = result
            override fun close() = Unit
        }

    private fun runThrowing(problem: Throwable): DiagnosticBackendRun =
        object : DiagnosticBackendRun {
            override fun execute(context: DiagnosticRunContext): DiagnosticRunResult = throw problem
            override fun close() = Unit
        }

    private fun input(
        logs: LogEmitter = LogEmitter { },
        cancellationRequested: Boolean = false,
        terminalCancelled: Boolean = false,
    ): BackendFlowInput {
        val identity = PublishedArtifactIdentity(
            device = 1uL,
            inode = 1uL,
            byteCount = 0L,
            sha256 = "0".repeat(64),
            mode = 0u,
        )
        return BackendFlowInput(
            runId = RUN_ID,
            generation = GENERATION,
            rootScript = RootScriptArtifact(
                runId = RUN_ID,
                generation = GENERATION,
                scriptPath = "/run/root-script/root-script.sh",
                resultPath = "/run/root-script/result",
                scriptIdentity = identity,
                resultIdentity = identity,
                retainedArtifacts = emptyList(),
            ),
            profile = ProfileSnapshot(
                "profile",
                ProfileSnapshot.CURRENT_SCHEMA_VERSION,
                "0".repeat(64),
            ),
            logs = logs,
            cancellation = RunCancellationView { cancellationRequested },
            reportContext = FlowReportContext(RunCapability.DIAGNOSTIC_ONLY, null, null),
            terminalClaim = FlowTerminalClaim { terminalCancelled },
        )
    }

    private data class FlowFixture(
        val flow: PluginBackendFlow,
        val owner: PluginRunResourceOwner,
    )

    private data class ExceptionCase(
        val code: PluginFailureCode,
        val primitive: StageResult,
        val dispatch: StageResult,
        val outcome: DiagnosticOutcome,
    )

    private companion object {
        const val RUN_ID = "0123456789abcdef0123456789abcdef"
        const val GENERATION = 7L

        val UNKNOWN_STAGE = StageResult(
            StageState.UNKNOWN,
            ResultReason.INTERNAL_CONTRACT_FAILURE,
            ExitKind.NONE,
            0,
        )

        val SUCCESS_RESULT = DiagnosticRunResult(
            PluginStageResult(
                PluginStageState.SUCCESS,
                PluginResultReason.NONE,
                PluginExitKind.NONE,
                0,
            ),
            PluginStageResult(
                PluginStageState.SUCCESS,
                PluginResultReason.NONE,
                PluginExitKind.PROCESS_EXIT,
                0,
            ),
        )
    }
}
