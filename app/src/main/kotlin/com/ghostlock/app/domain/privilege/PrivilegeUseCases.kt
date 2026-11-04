package com.ghostlock.app.domain.privilege

import kotlinx.coroutines.flow.StateFlow

/** Observes the single application-scoped privilege authority. */
class ObservePrivilegeUseCase(
    private val port: PrivilegePort,
) {
    /** @return Hot privilege state owned by the injected data adapter. */
    operator fun invoke(): StateFlow<PrivilegeState> = port.state
}

/** Refreshes live Shizuku facts through the platform-neutral privilege boundary. */
class RefreshPrivilegeUseCase(
    private val port: PrivilegePort,
) {
    /** Refreshes the current state without changing user preference. */
    operator fun invoke() = port.refresh()
}

/** Handles one explicit executor-selection command. */
class SelectPrivilegeExecutorUseCase(
    private val port: PrivilegePort,
) {
    /**
     * @param kind Requested executor family.
     * @return Exact adapter outcome used by presentation to keep or change the dialog page.
     */
    operator fun invoke(kind: AttackExecutorKind): ExecutorSelectionResult = port.select(kind)
}
