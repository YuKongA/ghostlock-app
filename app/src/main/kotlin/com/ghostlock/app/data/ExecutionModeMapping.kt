package com.ghostlock.app.data

import com.ghostlock.app.data.component.ComponentAvailability
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.component.FrontendKind
import com.ghostlock.app.domain.model.ExecutionMode

/** Entry a mode launches from: the app process or a shell (Shizuku/UMH). */
enum class ExecutionEntry { App, Shell }

/*
 * Single mapping from the app-level [ExecutionMode] (ADR-0004 R18/R19) to the
 * profile/native selection it derives: which process entry, which backend, which
 * StepSet (backend private section) and which terminal the run targets.
 *
 * The mode itself never rides the wire; these derived values are what the
 * profile builder / runner consume. The catalogue is sparse: 43499 pairs with
 * {w1_w3|w1_w2} x root_child, 43284 only with pagecache_write x umh_forward.
 * Selecting the 43284 backend (or the UMH mode that requires it) therefore
 * overrides the mode-derived StepSet/terminal so the app can never address an
 * uncatalogued triple.
 */

/**
 * S4 R6b derived view: the combination token is the selection source; the
 * legacy [ExecutionMode] is computed from it for logs/compat, never the other
 * way around. 43284 is the UMH backend; W1W2 is the Shizuku step set; every
 * other token is the general app-entry path.
 */
fun CombinationSpec.toExecutionMode(): ExecutionMode = when {
    backend == BackendKind.Cve2026_43284 -> ExecutionMode.Umh
    steps == StepSetKind.W1W2 -> ExecutionMode.Shizuku
    else -> ExecutionMode.General
}

/** The token a legacy catalogued triple maps back to, when one exists. */
fun ExecutionSelection.asCombination(): CombinationSpec? =
    CombinationCatalog.fromDerived(backend, steps, terminal)

/** General = app entry (zygote, seccomp present). */
val ExecutionMode.entry: ExecutionEntry
    get() = if (this == ExecutionMode.General) ExecutionEntry.App else ExecutionEntry.Shell

/** General runs W1-W3; Shizuku stops after W1-W2; UMH is the 43284 vocabulary. */
val ExecutionMode.steps: StepSetKind
    get() = when (this) {
        ExecutionMode.General -> StepSetKind.W1W3
        ExecutionMode.Shizuku -> StepSetKind.W1W2
        ExecutionMode.Umh -> StepSetKind.PAGE_CACHE_WRITE
    }

/** General/Shizuku hand off to the root child; UMH forwards over the terminal. */
val ExecutionMode.terminal: FrontendKind
    get() = if (this == ExecutionMode.Umh) FrontendKind.UmhForward else FrontendKind.RootChild

/** The backend a mode belongs to; UMH is the only 43284 entry. */
val ExecutionMode.backend: BackendKind
    get() = if (this == ExecutionMode.Umh) BackendKind.Cve2026_43284 else BackendKind.Cve2026_43499

/** False while the native catalog keeps this terminal unavailable. */
val ExecutionMode.isAvailable: Boolean
    get() = ComponentAvailability.frontendAvailable(terminal)

/** Only the Shizuku entry needs the Shizuku shell (and its permission) ready. */
val ExecutionMode.requiresShizuku: Boolean
    get() = this == ExecutionMode.Shizuku

/**
 * True when the native must run in the **shell domain** (Shizuku) instead of the
 * app process.
 *
 * This is the single place that decides the route. It used to force the
 * cve_2026_43284 backend onto Shizuku because the app domain could not read the
 * device facts that backend collected (/proc/version, the /vendor/lib64 candidate
 * list, the SELinux enforce state). The native now degrades an unreadable fact to
 * "unknown" and reports `device_facts degraded=...` instead of failing closed, so
 * 43284 runs in the app domain like the upstream exploit; Shizuku remains an
 * explicit, optional choice ([ExecutionMode.Shizuku]). The backend parameter is
 * kept so a future backend-specific route policy has one place to live.
 */
fun runRequiresShizuku(
    mode: ExecutionMode,
    @Suppress("UNUSED_PARAMETER") backend: BackendKind,
): Boolean = mode.requiresShizuku

/**
 * A catalogued sparse-triple selection: the backend plus the StepSet and
 * terminal the native catalog wires it to.
 */
data class ExecutionSelection(
    val backend: BackendKind,
    val steps: StepSetKind,
    val terminal: FrontendKind,
)

/**
 * Resolves the UI [mode] and the backend preference to a catalogued triple.
 * 43284 has exactly one catalogued StepSet/terminal (pagecache_write /
 * umh_forward) and no route, so selecting it -- or UMH, which requires it --
 * overrides the mode-derived values. Every other selection keeps the existing
 * 43499 mode mapping, byte-for-byte.
 */
fun resolveExecutionSelection(mode: ExecutionMode, backend: BackendKind): ExecutionSelection =
    if (backend == BackendKind.Cve2026_43284 || mode == ExecutionMode.Umh) {
        ExecutionSelection(
            backend = BackendKind.Cve2026_43284,
            steps = StepSetKind.PAGE_CACHE_WRITE,
            terminal = FrontendKind.UmhForward,
        )
    } else {
        ExecutionSelection(
            backend = BackendKind.Cve2026_43499,
            steps = mode.steps,
            terminal = mode.terminal,
        )
    }
