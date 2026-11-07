package com.ghostlock.app.backend.backends.diagnose.jna

import com.sun.jna.Library
import com.sun.jna.Structure

/**
 * Direct JNA mapping for the single release-coupled Diagnose ordinary-C operation.
 *
 * @note This type is the direct call mapping released with the same app and DSO. It owns no singleton, loader,
 *       fd, callback, compatibility branch, or lifecycle state.
 */
internal interface DiagnoseJnaLibrary : Library {
    /**
     * Executes one synchronous process-identity and nonce round trip.
     *
     * @param request Caller-owned request, or null for the native null-input contract.
     * @param result Caller-owned result, or null for the native null-output contract.
     * @return The sole signed 32-bit native status authority.
     */
    fun gl_diagnose_run(
        request: Request.ByReference?,
        result: Result.ByReference?,
    ): Int

    /** Caller-owned request mapped directly to the simultaneously released native definition. */
    @Structure.FieldOrder("nonce", "nativeLogFd", "shellLogFd")
    open class Request : Structure() {
        @JvmField
        var nonce: Long = 0L

        @JvmField
        var nativeLogFd: Int = -1

        @JvmField
        var shellLogFd: Int = -1

        /** By-reference marker required by the C pointer signature. */
        class ByReference : Request(), Structure.ByReference
    }

    /** Caller-owned result committed once after native returns from common handoff. */
    @Structure.FieldOrder("uid", "pid", "echoedNonce", "handoffStatus")
    open class Result : Structure() {
        @JvmField
        var uid: Int = 0

        @JvmField
        var pid: Int = 0

        @JvmField
        var echoedNonce: Long = 0L

        @JvmField
        var handoffStatus: Int = Int.MIN_VALUE

        /** By-reference marker required by the C pointer signature. */
        class ByReference : Result(), Structure.ByReference
    }
}
