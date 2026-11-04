package com.ghostlock.app.data.backend

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.backend.launcher.RunCoordinator
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.domain.run.RunCommand
import com.ghostlock.app.domain.run.RunExecutionResult
import com.ghostlock.app.domain.run.RunLogLevel
import com.ghostlock.app.domain.run.RunSessionPort
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.domain.privilege.ObservePrivilegeUseCase
import com.ghostlock.app.domain.settings.ObserveOperationalPreferencesUseCase
import com.ghostlock.app.domain.backend.BackendRegistryIdentity
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.log
import com.ghostlock.app.backend.launcher.BackendRunDispatch
import com.ghostlock.app.backend.launcher.BackendRunDispatcher
import com.ghostlock.app.backend.launcher.BackendRunGate
import com.ghostlock.app.backend.launcher.BackendExecutionState
import com.ghostlock.app.backend.declarations.ReadinessReason
import com.ghostlock.app.backend.launcher.RunDecision
import com.ghostlock.app.backend.launcher.RunPageContext
import java.io.File
import kotlinx.coroutines.flow.StateFlow

/**
 * Android/backend adapter that maps the existing authoritative gate and dispatcher to the run domain.
 *
 * @param context Application-capable context used for resources, launcher dispatch, and log sharing.
 * @param registryEntries Immutable registry membership captured once by the application composition root.
 * @param selectedKey Reads the current stable selection without exposing its global owner inward.
 * @param coordinator Application-scoped owner of the physical launcher coroutine.
 * @note The adapter does not reinterpret attack results; it only projects existing outer contracts.
 */
class BackendRunSessionAdapter(
    context: Context,
    registryEntries: List<BackendSpec>,
    private val selectedKey: () -> String?,
    private val coordinator: RunCoordinator,
    observeOperationalPreferences: ObserveOperationalPreferencesUseCase,
    observePrivilege: ObservePrivilegeUseCase,
    private val registryIdentity: BackendRegistryIdentity,
) : RunSessionPort {
    private val applicationContext = context.applicationContext
    private val entries = registryEntries.toList()
    private val operationalPreferences = observeOperationalPreferences()
    private val privilege = observePrivilege()

    override val state: StateFlow<RunSessionState> = coordinator.state

    /** Refreshes selection, current gate output, and presentation preferences when no run is active. */
    override fun refresh() {
        coordinator.updateContext(buildContext(currentSpec()))
    }

    /**
     * Re-resolves the stable backend key and invokes the unchanged existing dispatcher once.
     *
     * @param command Stable-key run request from the ViewModel.
     * @return True only when the application coordinator accepted ownership of a new run.
     */
    override fun start(command: RunCommand): Boolean {
        val spec = entries.firstOrNull { candidate ->
            candidate.key == command.backendKey && candidate.selectable
        } ?: run {
            refresh()
            return false
        }
        if (currentSpec()?.key != command.backendKey) {
            refresh()
            return false
        }
        val decision = decision(spec)
        val context = buildContext(spec, decision)
        if (!decision.allowed) {
            coordinator.updateContext(context)
            return false
        }
        return coordinator.start(context) { onLine ->
            mapDispatch(
                BackendRunDispatcher.dispatch(
                    spec = spec,
                    decision = decision,
                    context = applicationContext,
                    executor = privilege.value.attackExecutor(),
                    onLine = onLine,
                    registryIdentity = registryIdentity,
                ),
            )
        }
    }

    /** Routes explicit cancellation to the application coordinator and existing launcher job. */
    override fun abort(): Boolean = coordinator.requestAbort(
        disabledMessage = applicationContext.getString(R.string.run_abort_disabled),
    )

    /** Clears only retained terminal facts. */
    override fun clearTerminal() = coordinator.clearTerminal()

    /**
     * Writes the current immutable terminal snapshot and launches the Android chooser.
     *
     * @param chooserTitle Localized chooser title.
     * @return True when file creation, URI grant, and chooser launch all succeeded.
     */
    override fun shareTerminal(chooserTitle: String): Boolean = runCatching {
        val directory = File(applicationContext.cacheDir, "logs").apply { mkdirs() }
        val file = File(
            directory,
            "ghostlock-terminal-" + System.currentTimeMillis() + ".log",
        )
        file.writeText(
            state.value.entries.joinToString("\n") { entry ->
                log.format(entry.level.toApplicationLogLevel(), entry.message)
            } + "\n",
        )
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
        val chooser = Intent.createChooser(send, chooserTitle).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        applicationContext.startActivity(chooser)
    }.isSuccess

    /** @return Selected captured spec, first selectable fallback, or null. */
    private fun currentSpec(): BackendSpec? {
        val key = selectedKey()
        return entries.firstOrNull { it.selectable && it.key == key }
            ?: entries.firstOrNull { it.selectable }
    }

    /** @return Existing authoritative gate output for the supplied spec and current executor. */
    private fun decision(spec: BackendSpec?): RunDecision = BackendRunGate.decide(
        spec = spec,
        readiness = spec?.readiness() ?: BackendReadiness.NotReady(
            listOf(ReadinessReason(R.string.backend_run_no_backend)),
        ),
        context = RunPageContext(
            backendRunInProgress = coordinator.isRunning(),
            executor = privilege.value.attackExecutor(),
        ),
    )

    /** @return Pure domain projection of outer backend, gate, and interaction preferences. */
    private fun buildContext(
        spec: BackendSpec?,
        currentDecision: RunDecision = decision(spec),
    ): RunSessionState = RunSessionState(
        backendKey = spec?.key,
        backendLabel = spec?.cve ?: "-",
        startAllowed = currentDecision.allowed,
        blockedReasons = currentDecision.reasons.map(::reasonText),
        abortable = spec?.abortable == true,
        warnOnRerun = operationalPreferences.value.warnOnRerun,
        showTimestamp = operationalPreferences.value.showLogTimestamp,
        terminalFontPath = operationalPreferences.value.terminalFontPath
            ?.takeIf { path -> File(path).isFile },
    )

    /** @return Existing localized reason text; diagnostic detail remains out of visible UI text. */
    private fun reasonText(reason: ReadinessReason): String =
        applicationContext.getString(reason.messageRes)

    /** @return Domain result that preserves the existing dispatch state without adding a verdict. */
    private fun mapDispatch(dispatch: BackendRunDispatch): RunExecutionResult = when (dispatch) {
        is BackendRunDispatch.Blocked -> RunExecutionResult.Blocked(
            dispatch.reasons.map(::reasonText),
        )

        is BackendRunDispatch.Executed -> when (val result = dispatch.state) {
            BackendExecutionState.Idle -> RunExecutionResult.Idle
            BackendExecutionState.Running -> RunExecutionResult.Running
            is BackendExecutionState.Refused -> RunExecutionResult.Refused(result.reasons)
            BackendExecutionState.HandoffRequested -> RunExecutionResult.HandoffRequested
            is BackendExecutionState.Finished -> when (val flow = result.result) {
                is FlowResult.Ok -> RunExecutionResult.Succeeded(flow.evidence)
                is FlowResult.Failed -> RunExecutionResult.Failed(flow.reason, flow.evidence)
            }
        }
    }

    /** @return Existing application log severity used by the historical export format. */
    private fun RunLogLevel.toApplicationLogLevel(): LogLevel = when (this) {
        RunLogLevel.Verbose -> LogLevel.Verbose
        RunLogLevel.Debug -> LogLevel.Debug
        RunLogLevel.Info -> LogLevel.Info
        RunLogLevel.Warn -> LogLevel.Warn
        RunLogLevel.Error -> LogLevel.Error
    }
}
