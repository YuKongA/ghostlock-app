package com.ghostlock.app.core.flow

/**
 * 一次 backend flow run 的请求标记。
 *
 * 每个 [Flow] 实现必须声明自己的具体子类，并在 [Flow] 的泛型参数中绑定；
 * core 不提供通用/默认 request，避免把 backend 字段解释集中到公共 DTO。
 */
interface FlowRequest
