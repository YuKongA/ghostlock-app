package com.ghostlock.app.core

/** A typed outcome; an uncertain native effect is an INDETERMINATE failure, never success. */
sealed interface AccessResult<out T> {
    data class Success<T>(val value: T) : AccessResult<T>
    data class Failure(val reason: AccessFailure, val detail: String) : AccessResult<Nothing>
}

/** Failure classes shared by both address domains. */
enum class AccessFailure { UNSUPPORTED, INVALID_REQUEST, NOT_READY, NATIVE_FAILURE, NO_EFFECT, INDETERMINATE }

/** A successful write receipt exists only for a positively verified effect. */
data class WriteReceipt(val verifiedBytes: Int) {
    init { require(verifiedBytes > 0) }
}
