package com.ghostlock.app.core.access

import com.ghostlock.app.core.NativeSession

/**
 * 数据面会话：拥有能力资源与 JNI 会话句柄，是能力视图的唯一 owner。
 *
 * 视图非拥有、永不 close；本会话关闭或重初始化后不得继续发布旧视图。
 * 它不参与 Flow 的节点执行，也不释放 node/端口句柄。
 *
 * open() 只创建会话/native 句柄；backend 初始化只在本接口的 initialize()。
 */
interface AccessSession : NativeSession {
    val capabilities: AccessCapabilities

    /**
     * backend 初始化唯一入口：绑定 profile/target/route 并调用 native initialize；
     * 成功后才发布能力视图，失败不得留下半发布视图。
     */
    suspend fun initialize(): AccessResult<InitializationReceipt>

    suspend fun snapshot(): AccessResult<SessionSnapshot>

    /** 幂等；负责 native 释放，必须与 close() 的取消路径互斥。 */
    suspend fun shutdown(): AccessResult<ShutdownReceipt>
}
