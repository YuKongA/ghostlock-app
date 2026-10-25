package com.ghostlock.app.backend.lua

import com.ghostlock.app.core.flow.FlowRequest

/**
 * Lua 的一次 run 请求。
 *
 * @property pathId 可选入口标识，原样暴露给脚本。
 * @property payload 脚本字节；优先于 [values] 中的 "script"。
 * @property values 结构化参数；脚本经 `request` 读取。
 */
class FlowLuaRequest(
    val pathId: String = "",
    val payload: ByteArray? = null,
    val values: Map<String, Any?> = emptyMap(),
) : FlowRequest
