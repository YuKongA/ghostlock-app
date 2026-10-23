package com.ghostlock.app.core.flow

/** UI 交给一个 backend flow 的一次请求。 */
data class FlowRequest(
    val backendClass: kotlin.reflect.KClass<out Flow>,
    val pathId: String = "",
    val payload: ByteArray? = null,
    val values: Map<String, Any?> = emptyMap(),
)
