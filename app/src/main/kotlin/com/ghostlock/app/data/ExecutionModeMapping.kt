package com.ghostlock.app.data

import com.ghostlock.app.data.component.ComponentAvailability
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.domain.model.ExecutionMode

/** Entry a mode launches from: the app process or a shell (Shizuku/UMH). */
enum class ExecutionEntry { App, Shell }

/*
 * Single mapping from the app-level [ExecutionMode] (ADR-0004 R18/R19) to the
 * profile/native selection it derives: which process entry, which StepSet
 * (backend.cve_2026_43499.steps) and which terminal the run targets.
 *
 * The mode itself never rides the wire; these derived values are what the
 * profile builder / runner consume. UMH is unavailable until the native
 * terminal_available(umh_forward) is true (T5), so its terminal availability is
 * delegated to [ComponentAvailability], the App-side mirror of the catalog.
 */

/** General = app entry (zygote, seccomp present). */
val ExecutionMode.entry: ExecutionEntry
    get() = if (this == ExecutionMode.General) ExecutionEntry.App else ExecutionEntry.Shell

/** General runs W1-W3; Shizuku/UMH run from shell and stop after W1-W2. */
val ExecutionMode.steps: StepSetKind
    get() = when (this) {
        ExecutionMode.General -> StepSetKind.W1W3
        ExecutionMode.Shizuku, ExecutionMode.Umh -> StepSetKind.W1W2
    }

/** General/Shizuku hand off to the root child; UMH forwards over the terminal. */
val ExecutionMode.terminal: FrontendKind
    get() = if (this == ExecutionMode.Umh) FrontendKind.UmhForward else FrontendKind.RootChild

/** False until the native catalog exposes this terminal (UMH until T5). */
val ExecutionMode.isAvailable: Boolean
    get() = ComponentAvailability.frontendAvailable(terminal)

/** Only the Shizuku entry needs the Shizuku shell (and its permission) ready. */
val ExecutionMode.requiresShizuku: Boolean
    get() = this == ExecutionMode.Shizuku
