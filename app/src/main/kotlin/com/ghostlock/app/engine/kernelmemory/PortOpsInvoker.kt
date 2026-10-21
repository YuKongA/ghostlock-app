package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.steps.OpsInvoker
import com.ghostlock.app.core.steps.OpsReply
import com.ghostlock.app.core.steps.StepId

/**
 * 把 PipelineEngine/StepPlans 的一次 write_word 映射成一次 43499 写事务。
 *
 * 地址是步骤的唯一键：StepPlans 已经决定 form/offset/value；这里只负责找到对应的
 * [KernelStep] 并交给端口。未知地址、重复地址、缺 form 都具名拒绝，不猜。
 */
class PortOpsInvoker(
    private val port: Backend43499Port,
    steps: Map<StepId, KernelStep>,
) : OpsInvoker {
    private val byAddress: Map<Long, KernelStep>

    init {
        require(steps.size == byAddressSize(steps)) { "step addresses must be unique" }
        byAddress = steps.values.associateBy { it.address }
        require(byAddress.size == steps.size) { "step addresses must be unique" }
    }

    override fun invoke(
        opName: String,
        values: Map<String, ULong>,
        blobs: Map<String, ByteArray>,
    ): OpsReply {
        if (opName != "write_word") {
            return refused("unsupported", "op-unknown:" + opName)
        }
        val address = values["address"]?.toLong()
            ?: return refused("bad-parameters", "key-missing:address")
        val step = byAddress[address]
            ?: return refused("bad-parameters", "step-address-unknown:" + address)
        val form = values["form"]?.toInt()
            ?: return refused("bad-parameters", "key-missing:form")
        if (form !in 1..3) return refused("bad-parameters", "form:" + form)
        val value = values["value"]?.toLong() ?: 0L
        val offset = values["offset"]?.toLong() ?: step.offset
        return when (val answer = port.writeStep(step, address, value, form, offset)) {
            is PortResult.Ok -> OpsReply(
                status = "ok",
                refusal = null,
                values = mapOf("width" to 8uL, "value" to value.toULong()),
                blobs = emptyMap(),
            )
            is PortResult.Refused -> {
                val status = when (answer.refusal.code) {
                    PortCode.IN_FLIGHT -> "indeterminate"
                    else -> answer.refusal.code.token
                }
                OpsReply(status, answer.refusal.toString(), emptyMap(), emptyMap())
            }
        }
    }

    private fun refused(status: String, reason: String): OpsReply =
        OpsReply(status, reason, emptyMap(), emptyMap())

    private fun byAddressSize(steps: Map<StepId, KernelStep>): Int =
        steps.values.map { it.address }.distinct().size
}
