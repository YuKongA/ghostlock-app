package com.ghostlock.app.data.ipsec

import com.ghostlock.app.data.profile.IpsecSessionSecrets

/**
 * Typed reason a transport-mode ESP SA could not be established. The factory
 * never throws, so a run can log the reason and stop instead of crashing.
 */
enum class IpsecSessionFailureReason {
    /** No IpSecManager system service (non-Android host / API < 28). */
    Unavailable,

    /** The platform rejected the operation (missing permission, appdomain). */
    PermissionDenied,

    /** Any other platform failure (socket/transform allocation). */
    BuildFailed,
}

/** Outcome of [IpsecSessionFactory.create]. */
sealed interface IpsecSessionResult {
    /** The SA is installed; the caller owns [session] and must close it. */
    data class Ready(val session: IpsecSession) : IpsecSessionResult

    /** No SA exists; [detail] is a platform message and never holds key bytes. */
    data class Failure(
        val reason: IpsecSessionFailureReason,
        val detail: String? = null,
    ) : IpsecSessionResult
}

/**
 * Owns the kernel resources that back [secrets] (transform, SPI reservation,
 * UDP encapsulation socket). They must stay open while the native process
 * sends ESP packets, and are released by [close]. The secrets themselves never
 * touch disk, argv or a log.
 */
interface IpsecSession : AutoCloseable {
    val secrets: IpsecSessionSecrets

    /** Releases every owned resource; idempotent. */
    override fun close()
}

/**
 * Establishes one transport-mode ESP SA (AES-CBC + HMAC-SHA256, ICV 128 bit)
 * and returns its runtime parameters. Injectable so tests and hosts without an
 * Android IpSecManager can substitute a fake or a fail-closed implementation.
 */
fun interface IpsecSessionFactory {
    /**
     * Builds the SA or returns a typed failure. Must not throw; an unexpected
     * platform exception is mapped to [IpsecSessionFailureReason.BuildFailed].
     */
    fun create(): IpsecSessionResult
}
