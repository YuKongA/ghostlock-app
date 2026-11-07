package com.ghostlock.app.data.backend

import android.content.Context
import android.content.Intent
import android.os.ParcelFileDescriptor
import androidx.core.content.FileProvider
import com.ghostlock.app.R
import com.ghostlock.app.backend.launcher.BackendRunGate
import com.ghostlock.app.backend.launcher.RunDecision
import com.ghostlock.app.backend.launcher.RunPageContext
import com.ghostlock.app.backend.transport.AppRunHostClient
import com.ghostlock.app.backend.transport.RunHostClientOutcome
import com.ghostlock.app.backend.transport.RunHostCode
import com.ghostlock.app.backend.transport.ShizukuRunHostClient
import com.ghostlock.app.core.run.LogHub
import com.ghostlock.app.core.run.FrozenRunInputLease
import com.ghostlock.app.core.run.RunInputFreezer
import com.ghostlock.app.data.run.copyLogPrefix
import com.ghostlock.app.data.run.AndroidLogSinkFactory
import com.ghostlock.app.domain.run.LogDocumentFormatter
import com.ghostlock.app.domain.run.initialLogRecords
import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.run.RunCommand
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunInputArtifactKind
import com.ghostlock.app.domain.run.LogFormatConfig
import com.ghostlock.app.domain.run.LogLevel
import com.ghostlock.app.domain.run.LogOrigin
import com.ghostlock.app.domain.run.LogRecord
import com.ghostlock.app.domain.run.LogSinkRequest
import com.ghostlock.app.domain.run.RunSessionPort
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.settings.ObserveOperationalPreferencesUseCase
import com.ghostlock.app.domain.settings.OperationalPreferences
import com.ghostlock.app.domain.settings.TerminalLogLevel
import com.ghostlock.app.run.FinishItemParcel
import com.ghostlock.app.run.PayloadItemParcel
import com.ghostlock.app.run.RunHostLogRecord
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunInputArtifactParcel
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext

/**
 * Android adapter from the domain run session to the shared typed App/Shizuku RunHost wire.
 *
 * @param registryEntries Immutable neutral registry membership.
 * @param availability Dynamic readiness resolver by stable key.
 * @param selectedKey Current selected stable key.
 * @param logHub Application-lifetime typed run owner.
 * @param freezer Single backend/profile/Payload/Finish freeze boundary.
 * @param appClient App `:flowhost` typed client.
 * @param shizukuClient Optional typed Shizuku client; absence fails the shell-domain gate closed.
 */
class BackendRunSessionAdapter(
    context: Context,
    registryEntries: List<BackendSpec>,
    private val availability: (String) -> BackendAvailability?,
    private val selectedKey: () -> String?,
    private val logHub: LogHub,
    private val formatter: LogDocumentFormatter,
    private val freezer: RunInputFreezer,
    private val appClient: AppRunHostClient,
    private val shizukuClient: ShizukuRunHostClient?,
    observeOperationalPreferences: ObserveOperationalPreferencesUseCase,
    observePrivilege: ObservePrivilegeUseCase,
) : RunSessionPort {
    private val applicationContext = context.applicationContext
    private val storageSinks = AndroidLogSinkFactory(applicationContext)
    private val entries = registryEntries.toList()
    private val operationalPreferences = observeOperationalPreferences()
    private val privilege = observePrivilege()

    override val state: StateFlow<RunSessionState> = logHub.state

    /** Refreshes current neutral spec, readiness, executor, and presentation preferences. */
    override fun refresh() {
        logHub.updateContext(buildContext(currentSpec()))
    }

    /** Freezes one input and dispatches it through the selected typed host client. */
    override fun start(command: RunCommand): Boolean {
        val spec = entries.firstOrNull { candidate ->
            candidate.key == command.backendKey && candidate.selectable
        } ?: run {
            refresh()
            return false
        }
        if (currentSpec()?.key != spec.key) {
            refresh()
            return false
        }
        val decision = decision(spec)
        val frozenPreferences = operationalPreferences.value
        val context = buildContext(spec, decision, frozenPreferences)
        if (!decision.allowed) {
            logHub.updateContext(context)
            return false
        }
        val lease = runCatching { freezer.freeze(spec) }.getOrElse {
            logHub.updateContext(
                context.copy(
                    startAllowed = false,
                    blockedReasons = listOf("Run input freeze failed: " + it::class.java.simpleName),
                ),
            )
            return false
        }
        val frozen = lease.input
        val requestResult = runCatching { lease.toWireRequest() }
        lease.close()
        val request = requestResult.getOrElse {
            logHub.updateContext(
                context.copy(
                    startAllowed = false,
                    blockedReasons = listOf("Typed RunHost request assembly failed: " + it::class.java.simpleName),
                ),
            )
            return false
        }
        val sinkRequest = LogSinkRequest(
            enabled = frozenPreferences.autoExportLogs,
            runId = frozen.runId,
            generation = frozen.generation,
            backendKey = frozen.backendKey,
            relativePath = frozenPreferences.logExportLocation,
            formatConfig = LogFormatConfig(
                showTimestamp = frozenPreferences.showLogTimestamp,
            ),
        )
        val runSinks = storageSinks.create(sinkRequest) { failure ->
            logHub.reportSinkFailure(sinkRequest, failure)
        }
        val started = logHub.start(context, sinkRequest, runSinks) execution@{ onLog ->
            try {
                val executor = privilege.value.attackExecutor()
                val outcome = when (executor.kind) {
                    AttackExecutorKind.APP -> appClient.run(request) { record ->
                        record.toDomainOrNull()?.let(onLog)
                    }

                    AttackExecutorKind.SHIZUKU -> {
                        val client = shizukuClient
                            ?: return@execution RunExecutionResult.Refused(
                                listOf("The typed Shizuku RunHost is not connected."),
                            )
                        client.run(request) { record -> record.toDomainOrNull()?.let(onLog) }
                    }
                }
                outcome.toDomain()
            } finally {
                request.artifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
            }
        }
        if (!started) {
            request.artifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
        }
        return started
    }

    /** Propagates explicit cancellation through the current typed client coroutine. */
    override fun abort(): Boolean = logHub.requestAbort(
        disabledMessage = applicationContext.getString(R.string.run_abort_disabled),
    )

    /** Clears presentation rows only. */
    override fun clearTerminal() = logHub.clearTerminal()

    /** Shares an exact flushed private-file prefix without materializing the whole log. */
    override suspend fun shareTerminal(chooserTitle: String): Boolean = withContext(Dispatchers.IO) {
        runCatching {
            val directory = File(applicationContext.cacheDir, "logs").apply { check(mkdirs() || isDirectory) }
            val file = File(directory, "ghostlock-terminal-" + System.currentTimeMillis() + ".log")
            val prefix = logHub.filePrefix()
            if (prefix == null) {
                file.outputStream().buffered().use { output ->
                    formatter.formatHeader(null).forEach(output::write)
                    val config = LogFormatConfig(showTimestamp = state.value.showTimestamp)
                    initialLogRecords(state.value.minimumLogLevel).forEach { record ->
                        output.write(formatter.formatRecord(record, config))
                    }
                    formatter.formatTrailer(state.value.phase).forEach(output::write)
                }
            } else {
                copyLogPrefix(prefix, file)
            }
            val uri = FileProvider.getUriForFile(
                applicationContext,
                applicationContext.packageName + ".ghostlock.fileprovider",
                file,
            )
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/x-log"
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_TITLE, file.name)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            applicationContext.startActivity(
                Intent.createChooser(send, chooserTitle).apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) },
            )
        }.isSuccess
    }

    /** Returns current selectable spec or the first selectable fallback. */
    private fun currentSpec(): BackendSpec? {
        val key = selectedKey()
        return entries.firstOrNull { spec -> spec.selectable && spec.key == key }
            ?: entries.firstOrNull(BackendSpec::selectable)
    }

    /** Returns current fail-closed gate decision. */
    private fun decision(spec: BackendSpec?): RunDecision = BackendRunGate.decide(
        spec = spec,
        availability = spec?.let { availability(it.key) } ?: BackendAvailability.NotReady(
            listOf(BackendAvailabilityReason("backend-missing", "No backend is selected.")),
        ),
        context = RunPageContext(
            backendRunInProgress = logHub.isRunning(),
            executor = privilege.value.attackExecutor(),
            shizukuTypedHostAvailable = shizukuClient != null,
        ),
    )

    /** Projects neutral selected-backend and gate facts into the existing renderer contract. */
    private fun buildContext(
        spec: BackendSpec?,
        currentDecision: RunDecision = decision(spec),
        preferences: OperationalPreferences = operationalPreferences.value,
    ): RunSessionState = RunSessionState(
        backendKey = spec?.key,
        backendLabel = spec?.presentation?.displayName ?: "-",
        startAllowed = currentDecision.allowed,
        blockedReasons = currentDecision.reasons,
        abortable = spec?.abortPolicy == AbortPolicy.SAFE_DURING_RUN,
        warnOnRerun = preferences.warnOnRerun,
        showTimestamp = preferences.showLogTimestamp,
        minimumLogLevel = preferences.logLevel.toLogLevel(),
        terminalFontPath = preferences.terminalFontPath?.takeIf { path -> File(path).isFile },
    )

    /** Maps shared transport outcomes without reading callback text. */
    private fun RunHostClientOutcome.toDomain(): RunExecutionResult = when (this) {
        is RunHostClientOutcome.Terminal -> RunExecutionResult.Completed(snapshot.toRunReport())
        is RunHostClientOutcome.Rejected -> RunExecutionResult.Refused(listOf("RunHost rejected: " + code.name))
        is RunHostClientOutcome.TransportFailure -> RunExecutionResult.TransportFailure(
            "RunHost transport failure: " + code.name,
        )
    }

}

/** Builds the generated typed request and opens read-only source descriptors. */
internal fun FrozenRunInputLease.toWireRequest(): RunHostRequest {
    val frozen = input
    val invocationByItemId = frozen.artifactInvocations.associateBy { invocation -> invocation.itemId }
    val openedArtifacts = mutableListOf<RunInputArtifactParcel>()
    try {
        frozen.inputArtifacts.forEach { artifact ->
            openedArtifacts += RunInputArtifactParcel().also { parcel ->
                parcel.logicalId = artifact.logicalId
                parcel.kind = artifact.kind.wireValue
                parcel.byteCount = artifact.byteCount
                parcel.sha256 = artifact.sha256
                parcel.fd = duplicate(artifact.sourceToken)
            }
        }
        return RunHostRequest().also { request ->
            request.runId = frozen.runId
            request.generation = frozen.generation
            request.backendKey = frozen.backendKey
            request.profileArtifactId = frozen.profile.artifactId
            request.artifacts = openedArtifacts
            request.payloadItems = frozen.payload.enabledItems().map { item ->
                PayloadItemParcel().also { parcel ->
                    parcel.itemId = item.id
                    parcel.required = item.required
                    when (item.kind) {
                        PayloadKind.Command -> {
                            require(item.scriptMode == 0) {
                                "script-mode Payload requires a staged USER_SCRIPT artifact"
                            }
                            parcel.kind = 1
                            parcel.artifactId = null
                            parcel.args = item.command.trim().split(Regex("\\s+")).filter(String::isNotEmpty)
                        }

                        PayloadKind.RootManager -> {
                            val invocation = requireNotNull(invocationByItemId[item.id]) {
                                "root-manager artifact invocation is missing"
                            }
                            parcel.kind = 3
                            parcel.artifactId = invocation.artifactId
                            parcel.args = invocation.args
                        }

                        PayloadKind.Lkm -> error("LKM artifact staging belongs to a carrier child plan")
                    }
                    parcel.acceptanceKind = when (item.acceptance) {
                        PayloadAcceptance.ExitZero -> 1
                        PayloadAcceptance.DaemonAck -> 2
                        PayloadAcceptance.ModulePresent -> 3
                    }
                }
            }
            request.finishItems = frozen.finish.items.map { item ->
                FinishItemParcel().also { parcel ->
                    parcel.itemId = item.id
                    parcel.kind = when (item.kind) {
                        FinishKind.Command -> 1
                        FinishKind.Script -> error("Finish script staging belongs to the file artifact adapter")
                        FinishKind.Reboot -> 3
                    }
                    parcel.severity = when (item.severity) {
                        FinishSeverity.Normal -> 1
                        FinishSeverity.SafetyCritical -> 2
                    }
                    parcel.alwaysRun = item.alwaysRun
                    parcel.artifactId = null
                    parcel.args = item.args
                }
            }
        }
    } catch (problem: Throwable) {
        openedArtifacts.forEach { artifact -> runCatching { artifact.fd.close() } }
        throw problem
    }
}

/** Decodes one validated wire log into the sole canonical record shape. */
private fun RunHostLogRecord.toDomainOrNull(): LogRecord? = runCatching {
    LogRecord(
        origin = requireNotNull(LogOrigin.fromWireValue(origin)),
        level = requireNotNull(LogLevel.fromWireValue(level)),
        message = message,
    )
}.getOrNull()

/** Maps the storage-neutral setting to the canonical run-log level. */
private fun TerminalLogLevel.toLogLevel(): LogLevel = when (this) {
    TerminalLogLevel.Verbose -> LogLevel.Verbose
    TerminalLogLevel.Debug -> LogLevel.Debug
    TerminalLogLevel.Info -> LogLevel.Info
    TerminalLogLevel.Warn -> LogLevel.Warn
    TerminalLogLevel.Error -> LogLevel.Error
}
