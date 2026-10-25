package com.ghostlock.app.core.nativeabi

/** JNI 调用的稳定顶层状态；具体 backend 事实由独立 DTO 承载。 */
enum class NativeCallStatus {
    OK,
    BAD_ARGUMENT,
    STALE_HANDLE,
    WRONG_KIND,
    INVALID_STATE,
    BUSY,
    CAPACITY,
    UNSUPPORTED,
    CANCELLED_CLEAN,
    CLEAN_FAILURE,
    DIRTY_FAILURE,
    INDETERMINATE,
}

/** 本次调用对 Native 物理资源造成的可观察影响。 */
enum class NativeEffect {
    CLEAN,
    DIRTY,
    INDETERMINATE,
}

/**
 * JNI 返回头的 Kotlin 表达。
 *
 * @param requestId 仅用于关联日志，不提供幂等重放。
 * @param status 调用状态。
 * @param errnoValue 原始 errno；与 [status] 相互独立。
 * @param effect 资源终态；DIRTY/INDETERMINATE 不得普通释放。
 */
data class NativeCallResult(
    val requestId: ULong,
    val status: NativeCallStatus,
    val errnoValue: Int = 0,
    val effect: NativeEffect = NativeEffect.CLEAN,
)
