package com.ghostlock.app.core.nativehandle

import com.ghostlock.app.core.nativeabi.NativeCallResult

/**
 * 一个 backend 私有 bridge 需要实现的最小资源终结端口。
 *
 * 该端口不提供通用 execute/opcode；具体操作留在 backend 自己的 typed bridge。
 */
internal fun interface NativeHandlePort {
    /** 终结 [id]；Native 必须再次校验 [scope] 与 handle owner 一致。 */
    fun close(id: NativeHandleId, scope: RunScopeToken): NativeCallResult
}
