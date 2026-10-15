package com.ghostlock.app.core.steps

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 步骤引擎的四条规则，用假的原生面来跑：
 *  1. 顺序 + 拒绝即停；2. 缺前置不动手；3. indeterminate ⇒ 冻结；4. 重试只按输入来。
 *
 * 这些都是纯逻辑，所以在 JVM 单测里跑；真机上的证据是门禁那次"计划跑不动、并说清卡在哪一步"。
 */
class StepEngineTest {
    /** 假的原生面：按 op 名回答预先排好的结果，并记录被调用过什么。 */
    private class FakeOps(private val answers: Map<String, List<OpsReply>>) : OpsInvoker {
        val calls = mutableListOf<String>()
        private val served = mutableMapOf<String, Int>()

        override fun invoke(
            opName: String,
            values: Map<String, ULong>,
            blobs: Map<String, ByteArray>,
        ): OpsReply {
            calls += opName
            val queue = answers[opName].orEmpty()
            val index = served.getOrDefault(opName, 0)
            served[opName] = index + 1
            return queue.getOrNull(index) ?: queue.lastOrNull() ?: ok()
        }

        private fun ok() = OpsReply("ok", null, emptyMap(), emptyMap())
    }

    private fun ok() = OpsReply("ok", null, emptyMap(), emptyMap())

    private fun refused(reason: String, status: String = "unsupported") =
        OpsReply(status, reason, emptyMap(), emptyMap())

    @Test
    fun aRefusalStopsTheRun() {
        val ops = FakeOps(mapOf("write_word" to listOf(refused("not-ready:prepare", "not-ready"))))
        val engine = StepEngine(ops)
        val results = engine.run(
            listOf(StepId.W1, StepId.W2, StepId.W3),
            StepInputs(
                selinuxAddress = 0x1000uL,
                credentialAddress = 0x2000uL,
                credentialValue = 0x3000uL,
                seccompAddress = 0x4000uL,
            ),
        )
        assertEquals(1, results.size)
        assertEquals(StepId.W1, results[0].step)
        assertEquals(StepOutcome.Refused, results[0].outcome)
        assertEquals("not-ready:prepare", results[0].reason)
        assertEquals(listOf("write_word"), ops.calls)
    }

    @Test
    fun aStepWithoutItsInputCallsNothing() {
        val ops = FakeOps(emptyMap())
        val engine = StepEngine(ops)
        val results = engine.run(listOf(StepId.W1), StepInputs())
        assertEquals(1, results.size)
        assertEquals(StepOutcome.Refused, results[0].outcome)
        assertEquals("precondition:w1-inputs", results[0].reason)
        assertTrue("缺前置不得发出任何原生调用", ops.calls.isEmpty())
    }

    @Test
    fun anIndeterminateAnswerFreezesTheEngine() {
        val ops = FakeOps(
            mapOf(
                "write_word" to listOf(OpsReply("indeterminate", "wait_terminal:inflight", emptyMap(), emptyMap())),
            ),
        )
        val engine = StepEngine(ops)
        val results = engine.run(
            listOf(StepId.W1, StepId.W2),
            StepInputs(
                selinuxAddress = 0x1000uL,
                credentialAddress = 0x2000uL,
                credentialValue = 0x3000uL,
            ),
        )
        assertEquals(StepOutcome.Frozen, results[0].outcome)
        assertEquals("wait_terminal:inflight", results[0].reason)
        assertEquals(StepOutcome.Frozen, results[1].outcome)
        assertEquals("engine-frozen", results[1].reason)
        assertEquals("冻结后不得再发调用", 1, ops.calls.size)
    }

    @Test
    fun retriesFollowTheInputsAndNothingElse() {
        val notReady = OpsReply("not-ready", "not-ready:discover", emptyMap(), emptyMap())
        val ops = FakeOps(mapOf("write_word" to listOf(notReady)))
        val engine = StepEngine(ops)
        val results = engine.run(
            listOf(StepId.W1),
            StepInputs(attempts = 3, settleMs = 0L, selinuxAddress = 0x1000uL),
        )
        assertEquals(StepOutcome.Refused, results[0].outcome)
        assertEquals("三次尝试（attempts=3）", 3, ops.calls.size)

        val single = FakeOps(mapOf("write_word" to listOf(notReady)))
        val once = StepEngine(single).run(
            listOf(StepId.W1),
            StepInputs(attempts = 1, selinuxAddress = 0x1000uL),
        )
        assertEquals(StepOutcome.Refused, once[0].outcome)
        assertEquals("attempts=1 ⇒ 只打一次", 1, single.calls.size)
    }

    @Test
    fun theHookStepRecordsWhatItReached() {
        /* hook = 先算计划（设备上能答），再解析目标（当前会具名拒绝）。 */
        val ops = FakeOps(
            mapOf(
                "hook_words" to listOf(OpsReply("ok", null, mapOf("branch_word" to 0x14012d83uL), emptyMap())),
                "resolve_target" to listOf(refused("op-not-wired:resolve_target")),
            ),
        )
        val engine = StepEngine(ops)
        val results = engine.run(
            listOf(StepId.Hook),
            StepInputs(hookPath = "/system/lib64/libc++.so".toByteArray()),
        )
        assertEquals(StepOutcome.Refused, results[0].outcome)
        assertEquals("op-not-wired:resolve_target", results[0].reason)
        assertEquals(listOf("hook_words", "resolve_target"), results[0].calls)
    }

    @Test
    fun theCatalogRefusesAnUnknownStepByName() {
        val (steps, refusal) = PlanCatalog.fromTokens(listOf("w1", "not-a-step"))
        assertTrue(steps.isEmpty())
        assertEquals("step-unknown:not-a-step", refusal)
        val (declared, none) = PlanCatalog.fromTokens(listOf("patch1", "carrier", "hook", "trigger", "wait"))
        assertEquals(null, none)
        assertEquals(5, declared.size)
    }
}
