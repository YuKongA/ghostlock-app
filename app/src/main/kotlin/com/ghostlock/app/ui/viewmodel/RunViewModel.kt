package com.ghostlock.app.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import com.ghostlock.app.domain.run.RunBackendUseCase
import com.ghostlock.app.domain.run.RunCommand
import com.ghostlock.app.domain.run.RunSessionPhase
import com.ghostlock.app.domain.run.RunSessionState
import com.ghostlock.app.ui.screen.run.RunUiPhase
import com.ghostlock.app.ui.screen.run.RunUiState
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import java.io.File

/**
 * Owns run-page interaction state while the application adapter owns physical launcher lifetime.
 *
 * @param runBackend Domain use case over the application-scoped run session.
 * @note ViewModel clearing cancels only this StateFlow subscription; it never cancels a backend run.
 */
class RunViewModel(
    private val runBackend: RunBackendUseCase,
) : ViewModel() {
    private val interaction = MutableStateFlow(RunInteraction())

    /** Single renderer contract shared by Material and Miuix production pages. */
    val uiState: StateFlow<RunUiState> = combine(runBackend.state, interaction, ::toUiState)
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.Eagerly,
            initialValue = toUiState(runBackend.state.value, interaction.value),
        )

    init {
        runBackend.refresh()
    }

    /** Refreshes selected backend, gate facts, and terminal presentation preferences. */
    fun refresh() = runBackend.refresh()

    /** Applies the current semantic primary-button transition exactly once. */
    fun onPrimaryAction() {
        val current = uiState.value
        if (current.startBlocked) return
        when (current.phase) {
            RunUiPhase.Idle -> startCurrent()
            RunUiPhase.Running -> runBackend.abort()
            RunUiPhase.Aborting -> Unit
            RunUiPhase.Aborted,
            RunUiPhase.Done,
            RunUiPhase.HandoffRequested -> interaction.value = RunInteraction(rerunReady = true)

            RunUiPhase.RerunReady -> {
                if (runBackend.state.value.warnOnRerun) {
                    interaction.value = interaction.value.copy(showRerunConfirmation = true)
                } else {
                    startCurrent()
                }
            }
        }
    }

    /** Clears terminal facts without terminating or detaching the physical run. */
    fun clearTerminal() = runBackend.clearTerminal()

    /** Opens the Android share flow through the data adapter. */
    fun shareTerminal(chooserTitle: String) {
        viewModelScope.launch { runBackend.shareTerminal(chooserTitle) }
    }

    /** Confirms the pending rerun and starts it through the same use case path. */
    fun confirmRerun() {
        interaction.value = RunInteraction()
        startCurrent()
    }

    /** Dismisses the pending confirmation while keeping the rerun-ready button state. */
    fun dismissRerun() {
        interaction.value = interaction.value.copy(showRerunConfirmation = false)
    }

    /** Starts the currently projected stable backend key after clearing page-local overlay state. */
    private fun startCurrent() {
        val current = uiState.value
        val backendKey = current.backendKey ?: return
        if (!current.startAllowed) return
        interaction.value = RunInteraction()
        runBackend.start(RunCommand(backendKey))
    }

    /** Maps pure session state plus page-local rerun overlay to the shared renderer contract. */
    private fun toUiState(
        session: RunSessionState,
        currentInteraction: RunInteraction,
    ): RunUiState {
        val sessionPhase = session.phase.toUiPhase()
        val phase = if (
            currentInteraction.rerunReady &&
            sessionPhase in setOf(
                RunUiPhase.Aborted,
                RunUiPhase.Done,
                RunUiPhase.HandoffRequested,
            )
        ) {
            RunUiPhase.RerunReady
        } else {
            sessionPhase
        }
        return RunUiState(
            backendKey = session.backendKey,
            backendLabel = session.backendLabel,
            startAllowed = session.startAllowed,
            blockedReasons = session.blockedReasons,
            abortable = session.abortable,
            showTimestamp = session.showTimestamp,
            terminalFontFamily = terminalFont(session.terminalFontPath),
            phase = phase,
            entries = session.entries,
            showRerunConfirmation = currentInteraction.showRerunConfirmation,
        )
    }

    /** Page-local state that never owns a launcher resource. */
    private data class RunInteraction(
        val rerunReady: Boolean = false,
        val showRerunConfirmation: Boolean = false,
    )
}

/** @return Renderer lifecycle equivalent of one domain lifecycle state. */
private fun RunSessionPhase.toUiPhase(): RunUiPhase = when (this) {
    RunSessionPhase.Idle -> RunUiPhase.Idle
    RunSessionPhase.Running -> RunUiPhase.Running
    RunSessionPhase.Aborting -> RunUiPhase.Aborting
    RunSessionPhase.Aborted -> RunUiPhase.Aborted
    RunSessionPhase.Done -> RunUiPhase.Done
    RunSessionPhase.HandoffRequested -> RunUiPhase.HandoffRequested
}

/** @return Resolved Compose font owned by presentation mapping, with a monospace fail-closed fallback. */
private fun terminalFont(path: String?): FontFamily {
    val file = path?.let(::File)
    return if (file?.isFile == true) FontFamily(Font(file)) else FontFamily.Monospace
}
