package com.ghostlock.app.core.steps

/**
 * 一步要做的事，翻译成原生调用序列。
 *
 * 每个步骤只在这里出现一次：引擎不认步骤语义，步骤不认执行顺序 —— 两件事各自一处，各自可测。
 * 输入缺哪个、步骤就拒绝哪儿（返回 null ⇒ precondition），而不是猜一个值。
 */
object StepPlans {
    /** 一次原生调用的形状。 */
    data class Call(
        val opName: String,
        val values: Map<String, ULong> = emptyMap(),
        val blobs: Map<String, ByteArray> = emptyMap(),
    )

    /** 一步的计划：至少一次调用。 */
    data class Plan(val calls: List<Call>)

    /** @return 这一步的调用序列，或 null（输入不全 ⇒ 该步应拒绝）。 */
    fun forStep(step: StepId, inputs: StepInputs): Plan? = when (step) {
        /* 43499：三个步骤都是"一次 8 字节写"，差别在落字形状（见 WriteForm 的映射表）、 */
        /* 目标地址与值；形状用线上编号传（1 保留清零 / 2 叶子清零 / 3 写值保留）。 */
        StepId.W1 -> inputs.selinuxAddress?.let { address ->
            Plan(
                listOf(
                    Call(
                        "write_word",
                        mapOf(
                            "address" to address,
                            "form" to 1uL,
                            "offset" to inputs.writeOffset,
                        ),
                    ),
                ),
            )
        }

        StepId.W2 -> {
            val address = inputs.credentialAddress
            val value = inputs.credentialValue
            if (address == null || value == null) {
                null
            } else {
                Plan(
                    listOf(
                        Call(
                            "write_word",
                            mapOf(
                                "address" to address,
                                "value" to value,
                                "form" to 3uL,
                                "offset" to inputs.writeOffset,
                            ),
                        ),
                    ),
                )
            }
        }

        StepId.W3 -> inputs.seccompAddress?.let { address ->
            Plan(
                listOf(
                    Call(
                        "write_word",
                        mapOf(
                            "address" to address,
                            "form" to 2uL,
                            "offset" to inputs.writeOffset,
                        ),
                    ),
                ),
            )
        }

        /* 43284：先解析目标（门），再动内容。解析不成 ⇒ 该步拒绝，后面的调用不发出。 */
        StepId.Patch1 -> inputs.patch1Path?.let { path ->
            Plan(
                listOf(
                    Call("resolve_target", mapOf("mode" to 1uL), mapOf("path" to path)),
                    Call("embedded_image", mapOf("mode" to 0uL)),
                ),
            )
        }

        StepId.Carrier -> inputs.carrierPath?.let { path ->
            Plan(
                listOf(
                    Call("resolve_target", mapOf("mode" to 2uL), mapOf("path" to path)),
                ),
            )
        }

        StepId.Hook -> inputs.hookPath?.let { path ->
            Plan(
                listOf(
                    /* 先算计划（这一步在设备上已经能答），再去读站点块、写分支字与载荷 —— */
                    /* 后两个调用需要门，未接线时会以 op-not-wired 具名拒绝。 */
                    Call("hook_words"),
                    Call("resolve_target", mapOf("mode" to 3uL), mapOf("path" to path)),
                ),
            )
        }

        StepId.Trigger -> inputs.triggerDelayMs?.let { delay ->
            Plan(listOf(Call("trigger", mapOf("delay_ms" to delay))))
        }

        StepId.Wait -> {
            val budget = inputs.waitBudgetMs
            val poll = inputs.waitPollMs
            if (budget == null || poll == null) {
                null
            } else {
                val blobs = inputs.moduleName?.let { mapOf("text" to it) } ?: emptyMap()
                Plan(listOf(Call("wait_terminal", mapOf("mode" to budget, "poll_ms" to poll), blobs)))
            }
        }
    }
}
