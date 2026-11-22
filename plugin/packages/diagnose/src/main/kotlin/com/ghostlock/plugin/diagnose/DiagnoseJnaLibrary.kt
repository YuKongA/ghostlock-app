package com.ghostlock.plugin.diagnose

import com.sun.jna.Library
import com.sun.jna.Structure

/** Private, same-package JNA mapping for the ordinary-C Diagnose operation. */
internal interface DiagnoseJnaLibrary : Library {
    fun gl_diagnose_run(request: Request.ByReference?, result: Result.ByReference?): Int

    @Structure.FieldOrder("nonce", "nativeLogFd", "shellLogFd")
    open class Request : Structure() {
        @JvmField var nonce: Long = 0L
        @JvmField var nativeLogFd: Int = -1
        @JvmField var shellLogFd: Int = -1

        class ByReference : Request(), Structure.ByReference
    }

    @Structure.FieldOrder("uid", "pid", "echoedNonce", "handoffStatus")
    open class Result : Structure() {
        @JvmField var uid: Int = 0
        @JvmField var pid: Int = 0
        @JvmField var echoedNonce: Long = 0L
        @JvmField var handoffStatus: Int = Int.MIN_VALUE

        class ByReference : Result(), Structure.ByReference
    }
}
