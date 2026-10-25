package com.ghostlock.app.core.nativehandle

/** Native registry ID；Kotlin 不解析其中的 slot、generation 或 epoch。 */
@JvmInline
internal value class NativeHandleId(val encoded: Long) {
    init {
        require(encoded != 0L) { "native handle id must not be zero" }
    }
}

/** 调用者可观察的 wrapper 生命周期；Native registry 保留更细的内部状态。 */
enum class NativeHandlePhase {
    OPEN,
    CLOSING,
    CLOSED,
    TRANSFERRED,
    QUARANTINED,
}
