package com.ghostlock.app.backend.declarations

import com.ghostlock.app.domain.privilege.AttackExecutorKind

/** Declares the process identity required by one backend plugin. */
enum class BackendUidRequirement {
    /** App and authorized Shizuku execution are both accepted. */
    ANY_UID,

    /** Only the app-process attack thread is accepted. */
    APP_UID,

    /** Only the authorized Shizuku shell process is accepted. */
    SHELL_UID_2000,
    ;

    /** @return Whether [kind] satisfies this declaration; authorization is checked separately. */
    fun satisfiedBy(kind: AttackExecutorKind): Boolean = when (this) {
        ANY_UID -> true
        APP_UID -> kind == AttackExecutorKind.APP
        SHELL_UID_2000 -> kind == AttackExecutorKind.SHIZUKU
    }
}
