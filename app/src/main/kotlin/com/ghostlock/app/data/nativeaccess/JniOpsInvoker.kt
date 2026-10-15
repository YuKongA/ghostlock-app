package com.ghostlock.app.data.nativeaccess

import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.OpsReply

/**
 * 把步骤引擎的"具名调用"接到 JNI 会话上。
 *
 * 两个方向的翻译都在这里：op/key 的 token ↔ 线上编号（编号的唯一权威是 native 导出的
 * manifest，Kotlin 侧的枚举表由 NativeProtocolTest 对拍）。未知 token 一律返回具名拒绝，
 * 不猜、不丢、不改写成别的 op —— 步骤引擎因此永远看到它要的那笔事务或一条说清原因的回答。
 */
class JniOpsInvoker(private val session: JniAccessSession) : OpsInvoker {
    override fun invoke(
        opName: String,
        values: Map<String, ULong>,
        blobs: Map<String, ByteArray>,
    ): OpsReply {
        val op = NativeProtocol.Op.entries.firstOrNull { it.token == opName }
            ?: return refusal("unsupported", "op-unknown:" + opName)
        val mappedValues = HashMap<NativeProtocol.Key, ULong>(values.size)
        for ((name, value) in values) {
            val key = NativeProtocol.Key.entries.firstOrNull { it.token == name }
                ?: return refusal("key-unknown", "key-unknown:" + name)
            mappedValues[key] = value
        }
        val mappedBlobs = HashMap<NativeProtocol.Key, ByteArray>(blobs.size)
        for ((name, payload) in blobs) {
            val key = NativeProtocol.Key.entries.firstOrNull { it.token == name }
                ?: return refusal("key-unknown", "key-unknown:" + name)
            mappedBlobs[key] = payload
        }
        return when (val result = session.invoke(op, mappedValues, mappedBlobs)) {
            is AccessResult.Success -> OpsReply(
                status = result.value.status.token,
                refusal = result.value.refusal,
                values = result.value.values,
                blobs = result.value.blobs,
            )

            is AccessResult.Failure -> refusal("call-failed", result.detail)
        }
    }

    private fun refusal(status: String, reason: String): OpsReply =
        OpsReply(status, reason, emptyMap(), emptyMap())
}
