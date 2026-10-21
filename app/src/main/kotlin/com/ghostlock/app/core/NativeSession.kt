package com.ghostlock.app.core

/** Lifecycle state owned by Kotlin; native resources remain owned by the backend. */
enum class SessionPhase { INITIALIZING, READY, FAILED, CLOSED }

/** Whether native state may still need cleanup after an indeterminate operation. */
enum class CleanupDisposition { CLEAN, DIRTY }

/**
 * Low-level native session lifetime. Capability sessions extend this in core.access.
 *
 * Implementations serialize operations with close. After CLOSED, no capability call may cross
 * into native code. A DIRTY session must not silently transition back to READY.
 */
interface NativeSession : AutoCloseable {
    val phase: SessionPhase
    val cleanup: CleanupDisposition
    /** Stop access, disarm and release native resources in that order, then close the session. */
    override fun close()
}
