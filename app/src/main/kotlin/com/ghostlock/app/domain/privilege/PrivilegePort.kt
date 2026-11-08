package com.ghostlock.app.domain.privilege

import kotlinx.coroutines.flow.StateFlow

/** Domain boundary for persisted executor choice and live Shizuku availability facts. */
interface PrivilegePort {
    /** Current privilege snapshot plus adapter-originated permission and binder changes. */
    val state: StateFlow<PrivilegeState>

    /** Refreshes live service and permission facts without changing the persisted executor choice. */
    fun refresh()

    /**
     * Selects an immediately usable executor or submits the required permission request.
     *
     * @param kind Requested executor family.
     * @return Explicit selection/request/unavailable outcome; no implicit fallback is persisted.
     */
    suspend fun select(kind: AttackExecutorKind): ExecutorSelectionResult
}
