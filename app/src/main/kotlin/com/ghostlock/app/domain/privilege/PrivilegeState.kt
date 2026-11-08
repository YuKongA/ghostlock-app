package com.ghostlock.app.domain.privilege

import kotlinx.serialization.Serializable

/** Process family selected for one backend execution. */
@Serializable
enum class AttackExecutorKind {
    APP,
    SHIZUKU,
}

/** Selected process family plus its current binary authorization fact. */
data class AttackExecutorState(
    val kind: AttackExecutorKind,
    val authorized: Boolean,
)

/**
 * Storage- and renderer-neutral privilege facts for the current application process.
 *
 * @property selectedExecutor Persisted executor choice; it remains visible when authorization is revoked.
 * @property appUid Linux uid of the application process.
 * @property shizukuAvailable Whether the Shizuku binder is currently reachable.
 * @property shizukuGranted Whether the reachable Shizuku service granted this application permission.
 */
data class PrivilegeState(
    val selectedExecutor: AttackExecutorKind,
    val appUid: Int,
    val shizukuAvailable: Boolean,
    val shizukuGranted: Boolean,
) {
    /** @return Exact selected executor fact consumed by the run gate and launcher. */
    fun attackExecutor(): AttackExecutorState = AttackExecutorState(
        kind = selectedExecutor,
        authorized = selectedExecutor == AttackExecutorKind.APP || shizukuGranted,
    )

    /** @return Currently usable executor shown by the compact home-page summary. */
    fun displayedExecutor(): AttackExecutorKind =
        if (selectedExecutor == AttackExecutorKind.SHIZUKU && shizukuGranted) {
            AttackExecutorKind.SHIZUKU
        } else {
            AttackExecutorKind.APP
        }
}

/** Result of handling one executor-selection command. */
enum class ExecutorSelectionResult {
    /** The requested executor was persisted and is immediately usable. */
    SELECTED,

    /** A Shizuku permission request was submitted; selection remains unchanged until a later tap. */
    PERMISSION_REQUESTED,

    /** Shizuku is absent, stopped, or too old for the permission API. */
    UNAVAILABLE,

    /** Durable executor selection failed and presentation must not navigate as if it succeeded. */
    PERSISTENCE_FAILED,
}
