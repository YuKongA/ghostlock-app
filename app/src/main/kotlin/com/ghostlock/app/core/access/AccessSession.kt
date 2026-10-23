package com.ghostlock.app.core.access

import com.ghostlock.app.core.NativeSession

/**
 * 数据面会话：只拥有能力视图与 JNI 会话句柄。
 *
 * 它不参与 Flow 的节点执行，也不释放 node/端口句柄。
 */
interface AccessSession : NativeSession {
    val capabilities: AccessCapabilities

    suspend fun initialize(): AccessResult<InitializationReceipt>

    suspend fun snapshot(): AccessResult<SessionSnapshot>

    /** 幂等；负责 native 释放，必须与 close() 的取消路径互斥。 */
    suspend fun shutdown(): AccessResult<ShutdownReceipt>
}
