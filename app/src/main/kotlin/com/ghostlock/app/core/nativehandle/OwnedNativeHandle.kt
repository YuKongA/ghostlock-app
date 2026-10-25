package com.ghostlock.app.core.nativehandle

import com.ghostlock.app.core.nativeabi.NativeCallResult
import com.ghostlock.app.core.nativeabi.NativeEffect

/**
 * Native 物理资源 wrapper 的公共生命周期骨架。
 *
 * 子类只添加 backend-specific typed 操作。裸 ID 永不通过公开 API 暴露；Cleaner 不参与释放。
 */
abstract class OwnedNativeHandle internal constructor(
    id: NativeHandleId,
    private val scope: RunResourceScope,
    private val port: NativeHandlePort,
) : AutoCloseable {
    private val lock = Any()
    private var id: NativeHandleId? = id
    private var currentPhase = NativeHandlePhase.OPEN

    init {
        scope.register(this)
    }

    /** 当前 wrapper 相位；只用于调用前 fail-closed 和诊断。 */
    val phase: NativeHandlePhase
        get() = synchronized(lock) { currentPhase }

    /** backend typed bridge 调用前取得 ID；非 OPEN 状态一律拒绝。 */
    internal fun requireOpenId(): NativeHandleId = synchronized(lock) {
        check(currentPhase == NativeHandlePhase.OPEN) {
            "native resource is not open: $currentPhase"
        }
        checkNotNull(id)
    }

    /** 根据一次 Native 结果传播 quarantine；返回原结果供调用者解释。 */
    protected fun record(result: NativeCallResult): NativeCallResult {
        if (result.effect != NativeEffect.CLEAN) {
            synchronized(lock) {
                if (currentPhase == NativeHandlePhase.OPEN) {
                    currentPhase = NativeHandlePhase.QUARANTINED
                }
            }
        }
        return result
    }

    /**
     * 把 owner ID 交给同 backend 的新 wrapper factory。
     *
     * 旧 wrapper 立即变为 TRANSFERRED；调用方必须在同一临界区登记新 owner。
     */
    internal fun detachForTransfer(): NativeHandleId = synchronized(lock) {
        val value = requireOpenId()
        id = null
        currentPhase = NativeHandlePhase.TRANSFERRED
        scope.unregister(this)
        value
    }

    override fun close() {
        scope.unregister(this)
        closeInternal()
    }

    internal fun closeFromScope() {
        closeInternal()
    }

    private fun closeInternal() {
        val value = synchronized(lock) {
            when (currentPhase) {
                NativeHandlePhase.CLOSED,
                NativeHandlePhase.TRANSFERRED,
                NativeHandlePhase.QUARANTINED,
                NativeHandlePhase.CLOSING,
                -> return
                NativeHandlePhase.OPEN -> {
                    currentPhase = NativeHandlePhase.CLOSING
                    checkNotNull(id)
                }
            }
        }
        val result = try {
            port.close(value, scope.token)
        } catch (problem: Throwable) {
            synchronized(lock) {
                currentPhase = NativeHandlePhase.QUARANTINED
            }
            throw problem
        }
        synchronized(lock) {
            if (result.effect == NativeEffect.CLEAN) {
                id = null
                currentPhase = NativeHandlePhase.CLOSED
            } else {
                currentPhase = NativeHandlePhase.QUARANTINED
            }
        }
    }
}
