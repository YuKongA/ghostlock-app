package com.ghostlock.app.backend.backends.cve43499.flow

import com.ghostlock.app.backend.backends.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.backends.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499DebugWritePlanner
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499GeneratorContext
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteGenerator
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499WriteRequest
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ActorExecuteStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ActorPrepareStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ChainCloseReceipt
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PrebuiltActivationStep
import com.ghostlock.app.backend.backends.cve43499.owned.Cve43499PrebuiltDiscardReceipt
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499ExecuteFacts
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LandingStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LifecycleApi
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499LifecycleReceipt
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementStep
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499UidProbe
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499VictimSnapshot
import com.ghostlock.app.backend.backends.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.backends.cve43499.owned.KernelsuGuardResult
import com.ghostlock.app.backend.backends.cve43499.owned.ProcessLimitsOutcome
import com.ghostlock.app.backend.backends.cve43499.victim.VictimFinishResult
import com.ghostlock.app.backend.backends.cve43499.victim.VictimProbeKind
import com.ghostlock.app.backend.backends.cve43499.victim.VictimSession
import com.ghostlock.app.backend.backends.cve43499.victim.VictimUidResult
import com.ghostlock.app.backend.backends.cve43499.w3.Cve43499W3HardGate
import com.ghostlock.app.backend.backends.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.backends.cve43499.owned.PlacementConfig
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.data.cve43499.DebugPrimitiveKind
import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.core.flow.StepResult
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499MulticastSpec
import com.ghostlock.app.jni.Native43499PrimitiveSpec
import java.io.File
import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * R3d 多写链 Flow 单测（fake lifecycle / fake generator / fake planner / fake verifier）。
 *
 * 覆盖：W1→victim→W1b→W2(+fast-repair)→W3 步骤顺序与 attempts 证据、fast-repair
 * 失败即中止、W1b 失败收口、W3 判据 UNSUPPORTED 的 fail-closed、target source 缺失。
 *
 * @note 纯 JVM 单测；不触 Native。验收步因缺文件/marker 证据必然 Refused，本测试只
 *   断言到 acceptance 的步骤序列与写链证据（不伪成功）。
 */
class Cve43499WriteChainFlowTest {

    @Test
    fun `write chain runs w1 victim w1b w2 repair w3 with attempt evidence`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val flow = testFlow(lifecycle)
        val result = flow.run(request(lifecycle, verifier))

        /* A1 finish victim-only：无 rooted victim ⇒ finish step 直接 Refused（不执行
         * App/su），步骤止于 finish.execute（不再进入 lifecycle.close/acceptance）。 */
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals("cve43499:finish.execute:su-fallback-refused", failed.reason)
        assertEquals(
            listOf(
                "cve43499.request.validate",
                "cve43499.write.w1",
                "cve43499.victim.start",
                "cve43499.write.w1b",
                "cve43499.write.w2",
                "cve43499.victim.probe",
                "cve43499.write.w3",
                "cve43499.finish.execute",
            ),
            failed.steps.map { it.name },
        )

        /* 步骤 evidence：W1 一次通过；W1b repaired；W2 两次尝试+两次 fast-repair
         * （首轮 uid=2000 未 root）；W3 一次尝试。 */
        assertEquals("write-chain", failed.evidence["writeMode"])
        assertEquals("multicast", failed.evidence["primitive"])
        assertEquals("1", failed.evidence["w1Attempts"])
        assertEquals("repaired", failed.evidence["w1bState"])
        assertEquals("1", failed.evidence["w1bWrites"])
        assertEquals("2", failed.evidence["w2Attempts"])
        assertEquals("2", failed.evidence["w2Repairs"])
        assertEquals("0", failed.evidence["w2Uid"])
        assertEquals("1", failed.evidence["w3Rounds"])
        assertEquals("1", failed.evidence["w3Attempts"])
        assertEquals("exact", failed.evidence["w3LeafDirection"])
        assertEquals("0", failed.evidence["uid"])
        assertTrue(
            "clean W1 path must not report retained",
            failed.evidence["retained"] == null || failed.evidence["retained"] == "false",
        )

        /* 事务序列：W1 一事务 + W1b 一事务 + W2 两事务 + repair 两事务 + W3
         * （leaf 探针 1 + flags 1 + mode 1）= 9 次完整 placement→landing→actor；
         * 每次都是新事务（多写/pselect 均重新落页，不允许复用旧页）。 */
        assertEquals(9, lifecycle.placements)
        assertEquals(9, lifecycle.landings)
        assertEquals(9, lifecycle.executions)
        assertEquals(9, lifecycle.closedChains)
        assertEquals(1, lifecycle.closeAllCalls)
        /* W1 链在 W1b 期间保持打开：W1b 之前没有关闭过 W1 链。 */
        assertTrue(lifecycle.opensBeforeFirstClose >= 2)
    }

    @Test
    fun `jni victim factory reaches spec lifecycle and victim starts after w1`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        var captured: (() -> VictimSession)? = null
        var used = 0
        val flow = Flow43499Impl(
            lifecycleFactory = { error("legacy lifecycle factory must not be used") },
            specLifecycleFactory = { spec ->
                captured = spec.victimFactory
                lifecycle
            },
            w3GateFactory = { AlwaysPassW3Gate() },
        )
        val victimFactory: () -> VictimSession = {
            used++
            fakeJniVictimSession()
        }
        val result = flow.run(request(lifecycle, verifier, victimFactory = victimFactory))
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals("cve43499:finish.execute:su-fallback-refused", failed.reason)
        val factory = captured
        assertTrue("runSpec.victimFactory must reach the lifecycle factory", factory != null)
        assertEquals("fake lifecycle must own victim startup", 0, used)
        factory!!.invoke()
        assertEquals(1, used)
        val steps = failed.steps.map { it.name }
        assertTrue(
            steps.indexOf("cve43499.write.w1") < steps.indexOf("cve43499.victim.start"),
        )
        assertTrue(
            steps.indexOf("cve43499.victim.start") < steps.indexOf("cve43499.write.w1b"),
        )
    }

    @Test
    fun `single w1 path keeps legacy step order unchanged`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L)))
        val flow = testFlow(lifecycle)
        val result = flow.run(requestWithoutChain(lifecycle))
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        /* A1：单 W1 路径同样 finish victim-only；无 rooted victim ⇒ finish step Refused。 */
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals("cve43499:finish.execute:su-fallback-refused", failed.reason)
        assertEquals(
            listOf(
                "cve43499.request.validate",
                "cve43499.victim.start",
                "cve43499.placement.prepare",
                "cve43499.page.land",
                "cve43499.actor.prepare",
                "cve43499.actor.execute",
                "cve43499.victim.probe",
                "cve43499.finish.execute",
            ),
            failed.steps.map { it.name },
        )
    }

    @Test
    fun `ksu loaded refuses before w1 and records evidence`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val flow = testFlow(lifecycle)
        val result = flow.run(
            request(
                lifecycle,
                verifier,
                ksuCheck = {
                    KernelsuGuardResult.Loaded(
                        signal = "proc-modules",
                        evidence = mapOf("procModules" to "loaded"),
                    )
                },
            ),
        )
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.request.validate", failed.failedStep)
        assertEquals(
            "cve43499:request.validate:ksu-loaded:proc-modules",
            failed.reason,
        )
        val steps = failed.steps.map { it.name }
        assertTrue(steps.none { it == "cve43499.write.w1" })
        assertEquals(0, lifecycle.executions)
        val preflightEvidence =
            (failed.steps.last().result as StepResult.Failed).evidence
        assertEquals("loaded", preflightEvidence["ksuGuard"])
        assertEquals("proc-modules", preflightEvidence["ksuSignal"])
        assertEquals("loaded", preflightEvidence["ksu.procModules"])
    }

    @Test
    fun `process limits failure refuses before w1`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val flow = testFlow(lifecycle)
        val result = flow.run(
            request(
                lifecycle,
                verifier,
                processLimitsRaiser = {
                    ProcessLimitsOutcome.Failed("process-limits:nofile:1")
                },
            ),
        )
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.request.validate", failed.failedStep)
        assertEquals(
            "cve43499:request.validate:process-limits:nofile:1",
            failed.reason,
        )
        assertTrue(failed.steps.map { it.name }.none { it == "cve43499.write.w1" })
    }

    @Test
    fun `preflight success evidence is merged into flow evidence`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val flow = testFlow(lifecycle)
        val result = flow.run(
            request(
                lifecycle,
                verifier,
                ksuCheck = {
                    KernelsuGuardResult.Absent(mapOf("procModules" to "unreadable"))
                },
                processLimitsRaiser = {
                    ProcessLimitsOutcome.Raised(
                        nofileSoftBefore = 1024,
                        nofileHard = 1L shl 20,
                        nofileSoftAfter = 1L shl 20,
                        nprocSoftBefore = 512,
                        nprocHard = 1L shl 20,
                        nprocSoftAfter = 1L shl 20,
                    )
                },
            ),
        )
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        /* A1：无 rooted victim ⇒ finish Refused（前置 evidence 仍随 Done 步骤透传）。 */
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals("cve43499:finish.execute:su-fallback-refused", failed.reason)
        assertEquals("absent", failed.evidence["ksuGuard"])
        assertEquals("unreadable", failed.evidence["ksu.procModules"])
        assertEquals("raised", failed.evidence["processLimits"])
        assertEquals("1048576", failed.evidence["limitsNofileAfter"])
        assertEquals("1048576", failed.evidence["limitsNprocAfter"])
    }

    @Test
    fun `dirty transaction triggers fail stop once with evidence before terminate`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            consumedDirtyOnExecuteCall = setOf(1),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val events = mutableListOf<String>()
        var terminatedReason: String? = null
        var evidence: Map<String, String> = emptyMap()
        val flow = testFlow(lifecycle)
        val result = flow.run(
            request(
                lifecycle,
                verifier,
                failStopEnabled = true,
                failStopTerminator = { reason ->
                    events += "terminate"
                    terminatedReason = reason
                },
                failStopEvidenceSink = { map ->
                    events += "evidence"
                    evidence = map
                },
            ),
        )
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w1", failed.failedStep)
        /* 先 evidence 后 terminate；只一次。 */
        assertEquals(listOf("evidence", "terminate"), events)
        assertTrue(terminatedReason!!.startsWith("fail-stop:write.w1:"))
        assertEquals("true", evidence["failStopTriggered"])
        assertEquals("write.w1", evidence["failStopStage"])
        assertEquals("true", evidence["failStopPrecededByRetained"])
        assertTrue(evidence["failStopReason"]!!.contains("actor-execute:consumed-dirty"))
    }

    @Test
    fun `clean path does not trigger fail stop`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        var terminated = 0
        val flow = testFlow(lifecycle)
        val result = flow.run(
            request(
                lifecycle,
                verifier,
                failStopEnabled = true,
                failStopTerminator = { terminated++ },
            ),
        )
        assertTrue(result is FlowResult.Failed)
        assertEquals(0, terminated)
    }

    @Test
    fun `retained lifecycle close triggers fail stop with named stage`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            closeReceipt = Cve43499LifecycleReceipt(
                termination = "victim=dirty;",
                allReleased = false,
                retained = true,
            ),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        var terminatedReason: String? = null
        val flow = testFlow(lifecycle)
        val result = flow.run(
            request(
                lifecycle,
                verifier,
                failStopEnabled = true,
                failStopTerminator = { terminatedReason = it },
            ),
        )
        assertTrue(result is FlowResult.Failed)
        assertTrue(
            "expected retained fail-stop stage, was $terminatedReason",
            terminatedReason!!.startsWith("fail-stop:lifecycle.close:") ||
                terminatedReason!!.startsWith("fail-stop:write.w2.close-all-cleanup:"),
        )
        assertTrue((result as FlowResult.Failed).reason.isNotBlank())
    }

    @Test
    fun `g05 prebuilt repair is used at w1 exit and recorded`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "ready",
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(fastRepairPrebuildEnabled = true),
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals(1, lifecycle.prebuildCalls)
        assertEquals(1, lifecycle.landPrebuiltCalls)
        assertEquals(1, lifecycle.activatePrebuiltCalls)
        assertEquals("prebuilt", failed.evidence["w2RepairMode"])
        assertTrue(failed.evidence["prebuildUs"] != null)
        assertTrue(failed.evidence["w2RepairUs"] != null)
        assertTrue(failed.evidence["w2RepairActivationUs"] != null)
    }

    @Test
    fun `g05 w2 chain close retained fails w2 with fail stop`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "ready",
            closeRetentions = ArrayDeque(listOf(false, false, true)),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        var terminations = 0
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(fastRepairPrebuildEnabled = true),
                failStopEnabled = true,
                failStopTerminator = { terminations++ },
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertEquals(
            "cve43499:write.w2:fast-repair-close-retained",
            failed.reason,
        )
        assertEquals(1, terminations)
        assertTrue(failed.steps.map { it.name }.none { it == "cve43499.write.w3" })
        assertTrue(failed.steps.map { it.name }.none { it == "cve43499.finish.execute" })
    }

    @Test
    fun `g05 repair actor close retained fails w2`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "ready",
            closeRetentions = ArrayDeque(listOf(false, false, true)),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(fastRepairPrebuildEnabled = true),
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertEquals(
            "cve43499:write.w2:fast-repair-close-retained",
            failed.reason,
        )
    }

    @Test
    fun `g05 dirty prebuild at w1b exit fails before w2`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "retained",
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        /* failStop.enabled=0：B1 不依赖终止，仍必须 Failed。 */
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(fastRepairPrebuildEnabled = true),
            ),
        )
        val failed = result as FlowResult.Failed
        /* Q1=B（task-73）：预建在 W1b 成功出口；dirty ⇒ W1b fail-stop，不进 W2。 */
        assertEquals("cve43499.write.w1b", failed.failedStep)
        assertTrue(failed.reason.startsWith("cve43499:write.w1b:fast-repair-prebuild-retained:"))
        val steps = failed.steps.map { it.name }
        assertTrue(steps.contains("cve43499.write.w1b"))
        assertTrue(steps.none { it == "cve43499.write.w2" })
        assertTrue(steps.none { it == "cve43499.write.w3" })
        assertTrue(steps.none { it == "cve43499.finish.execute" })
        assertEquals(0, lifecycle.activatePrebuiltCalls)
    }

    @Test
    fun `g05 prebuild clean failure requires fallback unless allowed`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "clean-failed",
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(
                    fastRepairPrebuildEnabled = true,
                    allowFullFallback = false,
                ),
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertTrue(failed.reason.startsWith("cve43499:write.w2:fast-repair-prebuilt-required:"))
        assertTrue(lifecycle.discardPrebuiltCalls >= 1)
        assertEquals(3, lifecycle.placements)
    }

    @Test
    fun `g05 prebuild clean failure allowed falls back with evidence`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "clean-failed",
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(
                    fastRepairPrebuildEnabled = true,
                    allowFullFallback = true,
                ),
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("full-fallback", failed.evidence["w2RepairMode"])
        assertEquals(9, lifecycle.placements)
    }

    @Test
    fun `g05 prebuild disabled keeps legacy path and prebuild counters zero`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "ready",
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(request(lifecycle, verifier))
        val failed = result as FlowResult.Failed
        assertEquals("none", failed.evidence["w2RepairMode"])
        assertEquals(0, lifecycle.prebuildCalls)
        assertEquals(0, lifecycle.activatePrebuiltCalls)
    }

    @Test
    fun `g05 prebuild happens after w1b and before w2`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "ready",
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(fastRepairPrebuildEnabled = true),
            ),
        )
        val prebuildIndex = lifecycle.events.indexOf("prebuild")
        assertTrue("prebuild must run", prebuildIndex >= 0)
        /* W1b 事务 close 之后才 prebuild（W1b 成功出口）。 */
        val w1bCloseIndex = lifecycle.events.indexOf("close-chain")
        assertTrue("W1b chain close must precede prebuild", w1bCloseIndex in 0 until prebuildIndex)
        /* prebuild 之后的下一个 placement 即 W2 事务。 */
        val nextPlacement = lifecycle.events.drop(prebuildIndex).indexOf("placement")
        val w2PlacementIndex = if (nextPlacement >= 0) prebuildIndex + nextPlacement else -1
        assertTrue("W2 placement must follow prebuild", w2PlacementIndex > prebuildIndex)
        /* activation 发生在 W2 成功之后。 */
        assertTrue(lifecycle.events.indexOf("activate-prebuild") > w2PlacementIndex)
    }

    @Test
    fun `g05 w1b dirty skips prebuild and w2`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L, 0L)),
            prebuildMode = "ready",
            routeMissOnExecuteCall = setOf(2),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                policy = policy().copy(
                    fastRepairPrebuildEnabled = true,
                    w1bWrites = 1,
                ),
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w1b", failed.failedStep)
        assertEquals(0, lifecycle.prebuildCalls)
        assertTrue(failed.steps.map { it.name }.none { it == "cve43499.write.w2" })
    }

    @Test
    fun `w1 verify retry lands a new page for every attempt`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(
                listOf(
                    Cve43499WriteVerifyVerdict.UNVERIFIED,
                    Cve43499WriteVerifyVerdict.VERIFIED,
                ),
            ),
            leafDirections = ArrayDeque(),
            w3Verdicts = ArrayDeque(),
        )
        val result = testFlow(lifecycle).run(
            request(
                lifecycle = lifecycle,
                verifier = verifier,
                targetSource = Cve43499WriteTargetSource { Cve43499TargetAcquisition.Missing("test-missing") },
                policy = policy().copy(w1Attempts = 2, w1bEnabled = false),
            ),
        )

        /* W1 首轮验收不过 ⇒ 关闭旧链、重新 prepare+land（新页）；第二轮通过后 W2
         * 因缺 target 停住。 */
        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertEquals("2", failed.evidence["w1Attempts"])
        assertEquals(2, lifecycle.placements)
        assertEquals(2, lifecycle.landings)
        /* 首轮失败链关闭 1 条 + W1b step 出口关闭 W1 链（即使 W1b 被 route 跳过）。 */
        assertEquals(2, lifecycle.closedChains)
    }

    @Test
    fun `fast repair route miss aborts w2 without retry`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 2000L)),
            routeMissOnExecuteCall = setOf(4),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val result = testFlow(lifecycle).run(request(lifecycle, verifier))

        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertTrue(failed.reason.startsWith("cve43499:write.w2:fast-repair-route-miss:"))
        /* 只有一次 W2 尝试（fast-repair 失败不重试）；repair 事务只发生一次。 */
        assertEquals(4, lifecycle.placements)
        assertEquals(1, lifecycle.closeAllCalls)
    }

    @Test
    fun `missing target source fails closed before w2`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(),
            w3Verdicts = ArrayDeque(),
        )
        val result = testFlow(lifecycle).run(
            request(lifecycle, verifier, targetSource = Cve43499WriteTargetSource { Cve43499TargetAcquisition.Missing("test-missing") }),
        )

        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertTrue(failed.reason.startsWith("cve43499:write.w2:write-targets-missing:"))
        /* 只有 W1 + W1b 的事务；W2 没有发起 placement。 */
        assertEquals(2, lifecycle.placements)
    }

    @Test
    fun `unsupported w3 probe fails closed`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(
                listOf(Cve43499WriteVerifyVerdict.UNSUPPORTED),
            ),
        )
        val result = testFlow(lifecycle).run(request(lifecycle, verifier))

        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w3", failed.failedStep)
        assertEquals("cve43499:write.w3:seccomp-probe-unsupported", failed.reason)
    }

    @Test
    fun `invalid chain policy fails closed at request validation`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(0L)))
        val invalid = policy().copy(w1Attempts = 0)
        val result = testFlow(lifecycle).run(
            request(lifecycle, FakeVerifier(), policy = invalid),
        )

        assertTrue(result is FlowResult.Failed)
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.request.validate", failed.failedStep)
        assertEquals("cve43499:request.validate:write-chain:attempts", failed.reason)
        assertEquals(0, lifecycle.placements)
    }

    @Test
    fun `w2 precheck rejects victim identity change`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L)),
            victimSnapshotTimes = ArrayDeque(listOf(777L, 778L)),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(),
        )
        val result = testFlow(lifecycle).run(request(lifecycle, verifier))
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w2", failed.failedStep)
        assertEquals(
            "cve43499:write.w2:w2-precheck:victim-identity-changed",
            failed.reason,
        )
        /* W2 没有发起 placement（仅 W1+W1b 两次）。 */
        assertEquals(2, lifecycle.placements)
    }

    @Test
    fun `w2 precheck rejects uid baseline change`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2000L, 0L)),
            victimSnapshotUids = ArrayDeque(listOf(2000L, 0L)),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(),
        )
        val result = testFlow(lifecycle).run(request(lifecycle, verifier))
        val failed = result as FlowResult.Failed
        assertEquals(
            "cve43499:write.w2:w2-precheck:uid-baseline-unexpected",
            failed.reason,
        )
        assertEquals(2, lifecycle.placements)
    }

    @Test
    fun `w2 precheck rejects untrusted target evidence`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2000L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(),
        )
        val untrusted = Cve43499WriteTargetSource { context ->
            Cve43499TargetAcquisition.Acquired(
                victimTaskAddress = VICTIM_TASK,
                evidence = evidenceFor(context.baseline).copy(votes = 10L),
            )
        }
        val result = testFlow(lifecycle).run(
            request(lifecycle, verifier, targetSource = untrusted),
        )
        val failed = result as FlowResult.Failed
        assertEquals(
            "cve43499:write.w2:w2-precheck:target-untrusted",
            failed.reason,
        )
        assertEquals(2, lifecycle.placements)
    }

    /* ==================================================================== */
    /* R3f/task-45：W1 墙拆除 + finish 交 rooted victim                       */
    /* ==================================================================== */

    @Test
    fun `w1 verify unsupported continues to w1b w2 w3`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Ok(0, "out-sha", false),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.UNSUPPORTED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        val result = testFlow(lifecycle).run(
            request(lifecycle, verifier, handoffOverride = handoff),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.acceptance", failed.failedStep)
        val steps = failed.steps.map { it.name }
        assertTrue(steps.contains("cve43499.write.w1b"))
        assertTrue(steps.contains("cve43499.write.w2"))
        assertTrue(steps.contains("cve43499.write.w3"))
        assertEquals("unsupported", failed.evidence["w1Verify"])
        assertEquals("verifier-unsupported", failed.evidence["w1VerifyReason"])
        assertEquals("repaired", failed.evidence["w1bState"])
        assertEquals("1", failed.evidence["w1Attempts"])
        /* W1 链在 W1b 期间保持打开（未因 UNSUPPORTED 被 close）。 */
        assertTrue(lifecycle.opensBeforeFirstClose >= 2)
    }

    @Test
    fun `w1 verify unverified exhausted continues to next stages`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Ok(0, "out-sha", false),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(
                listOf(
                    Cve43499WriteVerifyVerdict.UNVERIFIED,
                    Cve43499WriteVerifyVerdict.UNVERIFIED,
                ),
            ),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                handoffOverride = handoff,
                policy = policy().copy(w1Attempts = 2),
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.acceptance", failed.failedStep)
        assertTrue(failed.steps.map { it.name }.contains("cve43499.write.w1b"))
        assertEquals("unverified", failed.evidence["w1Verify"])
        assertEquals("verify-unverified", failed.evidence["w1VerifyReason"])
        assertEquals("2", failed.evidence["w1Attempts"])
        assertEquals("repaired", failed.evidence["w1bState"])
    }

    @Test
    fun `w1 route miss still fails`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            routeMissOnExecuteCall = setOf(1),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(),
            leafDirections = ArrayDeque(),
            w3Verdicts = ArrayDeque(),
        )
        val result = testFlow(lifecycle).run(request(lifecycle, verifier))
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.write.w1", failed.failedStep)
        assertTrue(failed.reason.startsWith("cve43499:write.w1:unverified:"))
        assertTrue(failed.steps.map { it.name }.none { it == "cve43499.write.w1b" })
    }

    @Test
    fun `finish runs inside rooted victim and records exit digest`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Ok(
                exitCode = 0,
                outputSha256 = "finish-out-sha",
                timedOut = false,
            ),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(7, 8, 9))
        var fallbackCalls = 0
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                handoffOverride = handoff,
                finishTimeoutMs = 12_345L,
                finishExecutor = { _, _ ->
                    fallbackCalls++
                    FinishExecutionEvidence("x", 0, false, "x", null, null)
                },
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.acceptance", failed.failedStep)
        assertEquals("0", failed.evidence["executionExitCode"])
        assertEquals("victim", failed.evidence["finishSource"])
        assertEquals("finish-out-sha", failed.evidence["executionOutputSha256"])
        assertEquals("false", failed.evidence["executionTimedOut"])
        assertEquals(0, fallbackCalls)
        assertEquals(1, lifecycle.finishCalls)
        assertEquals(handoff.scriptPath, lifecycle.lastFinishScriptPath)
        assertEquals(12_345L, lifecycle.lastFinishTimeoutMs)
    }

    @Test
    fun `finish script sha mismatch refuses before victim`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Ok(0, "sha", false),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        /* 磁盘内容是 [1,2,3]，但 handoff 冻结的是 [4,5,6] ⇒ SHA 不匹配。 */
        val handoff = tempHandoff(
            fileBytes = byteArrayOf(1, 2, 3),
            handoffBytes = byteArrayOf(4, 5, 6),
        )
        val result = testFlow(lifecycle).run(
            request(lifecycle, verifier, handoffOverride = handoff),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertTrue(failed.reason.contains("script-sha-mismatch"))
        assertEquals(0, lifecycle.finishCalls)
    }

    @Test
    fun `finish victim failure is fail closed without su fallback`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Failed("victim-finish:timeout"),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        var fallbackCalls = 0
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                handoffOverride = handoff,
                finishExecutor = { _, _ ->
                    fallbackCalls++
                    FinishExecutionEvidence("x", 0, false, "x", null, null)
                },
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertTrue(failed.reason.contains("victim-finish:timeout"))
        assertEquals(0, fallbackCalls)
    }

    @Test
    fun `app su fallback success is refused without rooted victim`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        var fallbackCalls = 0
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                handoffOverride = handoff,
                finishExecutor = { _, runId ->
                    fallbackCalls++
                    /* A1 后该 seam 不再执行；即便注入“静默成功”也必须无调用+Refused。 */
                    FinishExecutionEvidence(runId, 0, false, "sha", null, null)
                },
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals("cve43499:finish.execute:su-fallback-refused", failed.reason)
        assertEquals("deprecated executor must never be invoked", 0, fallbackCalls)
    }

    @Test
    fun `no rooted victim refuses without running app fallback executor`() = runBlocking {
        val lifecycle = FakeLifecycle(victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)))
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        var fallbackCalls = 0
        val result = testFlow(lifecycle).run(
            request(
                lifecycle,
                verifier,
                handoffOverride = handoff,
                finishExecutor = { _, runId ->
                    fallbackCalls++
                    FinishExecutionEvidence(runId, -1, false, null, null, null)
                },
            ),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals("cve43499:finish.execute:su-fallback-refused", failed.reason)
        assertEquals(0, fallbackCalls)
        assertEquals(0, lifecycle.finishCalls)
        /* 不产生 App fallback 证据字段（未进入 acceptance）。 */
        assertNull(failed.evidence["finishSource"])
        assertNull(failed.evidence["executionExitCode"])
    }

    @Test
    fun `g05 finish timeout with retained victim records victimRetained evidence`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Failed(
                "victim-finish:timeout",
                retained = true,
            ),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        val result = testFlow(lifecycle).run(
            request(lifecycle, verifier, handoffOverride = handoff),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.finish.execute", failed.failedStep)
        assertEquals(1, lifecycle.finishCalls)
        val step = failed.steps.last().result as StepResult.Failed
        assertEquals("true", step.evidence["victimRetained"])
    }

    @Test
    fun `rooted child parked evidence is recorded`() = runBlocking {
        val lifecycle = FakeLifecycle(
            victimUids = ArrayDeque(listOf(2_000L, 0L, 0L)),
            rooted = true,
            victimFinishResult = VictimFinishResult.Ok(0, "out-sha", false),
            closeReceipt = Cve43499LifecycleReceipt(
                termination = "chain0:actor=released;victim=rooted-child-parked;",
                allReleased = true,
                retained = false,
                parkedRootedChild = true,
            ),
        )
        val verifier = FakeVerifier(
            w1Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
            leafDirections = ArrayDeque(listOf(Cve43499LeafDirection.EXACT)),
            w3Verdicts = ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        )
        val handoff = tempHandoff(byteArrayOf(1, 2, 3))
        val result = testFlow(lifecycle).run(
            request(lifecycle, verifier, handoffOverride = handoff),
        )
        val failed = result as FlowResult.Failed
        assertEquals("cve43499.acceptance", failed.failedStep)
        assertEquals("rooted-child-parked", failed.evidence["retained"])
        assertEquals("true", failed.evidence["rootedChildParked"])
        assertEquals("true", failed.evidence["resourcesReleased"])
    }

    /**
     * 建临时脚本对象：磁盘写 [fileBytes]，handoff 冻结 [handoffBytes]。
     *
     * @param fileBytes 磁盘实际内容。
     * @param handoffBytes handoff 冻结内容（默认与磁盘一致）。
     * @return 指向临时文件的 handoff。
     */
    private fun tempHandoff(
        fileBytes: ByteArray,
        handoffBytes: ByteArray = fileBytes,
    ): FinishHandoff {
        val path = Files.createTempFile("r3f-finish", ".sh")
        Files.write(path, fileBytes)
        return requireNotNull(
            FinishHandoff.create(path.toAbsolutePath().toString(), handoffBytes),
        )
    }

    /* ==================================================================== */
    /* 测试夹具                                                             */
    /* ==================================================================== */

    /**
     * 测试用 acquisition：默认返回固定地址 + 与 context 基线一致的 evidence
     * （passes=2、votes=200、candidates=512、threshold=128，均满足 trusted）。
     *
     * @param context Flow 提供的采样上下文（baseline 必须原样放进 evidence）。
     * @return Acquired(VICTIM_TASK, evidence)。
     */
    private fun acquisitionFor(
        context: Cve43499VictimContext,
    ): Cve43499TargetAcquisition = Cve43499TargetAcquisition.Acquired(
        victimTaskAddress = VICTIM_TASK,
        evidence = evidenceFor(context.baseline),
    )

    /**
     * @param baseline 采样时基线。
     * @return 满足 trusted 的默认 evidence（测试用；不含真实 perf 事实）。
     */
    private fun evidenceFor(baseline: Cve43499VictimBaseline): Cve43499TargetEvidence =
        Cve43499TargetEvidence(
            baseline = baseline,
            passes = 2,
            passAddresses = listOf(VICTIM_TASK, VICTIM_TASK),
            votes = 200L,
            candidates = 512L,
            threshold = 128L,
            spVotes = 2L,
            alignmentOk = true,
            spEvidenceOk = true,
            requireAlignment = false,
            requireSpEvidence = false,
            settleUs = 0L,
            closeRetained = false,
        )

    /**
     * 单 W1 兼容请求（`writeChain=null`；历史 R3b 路径）。
     *
     * @param lifecycle fake 生命周期。
     * @return 冻结兼容输入（landing+primitive）的请求。
     */
    private fun requestWithoutChain(lifecycle: FakeLifecycle): Flow43499Request {
        val handoff = requireNotNull(FinishHandoff.create("/tmp/single-w1-test.sh", byteArrayOf(1, 2, 3)))
        val runSpec = Flow43499RunSpec(
            placement = placementConfig(),
            actorSetup = actorSetup(),
            elapsedRealtimeNanos = { System.nanoTime() },
            frozenInputs = Cve43499GeneratedInputs(
                landing = LandingConfig(
                    finalBytes = ByteArray(0x10000),
                    contentGeneration = 1L,
                    timeoutNanos = 1_000_000_000L,
                    socketSendBufferSize = 1L shl 20,
                    reclaimSendLimit = 4L,
                    reclaimPayloadSize = 0x10000L,
                    fillByte = 0x41L,
                    pageAcceptRequired = 1L,
                    pageAcceptOffset = 0x100L,
                    pageAcceptBit = 16L,
                ),
                primitive = Native43499MulticastSpec(
                    finalWaiterAddress = 0x1111L,
                    finalTaskAddress = 0x2222L,
                    finalLockAddress = 0x3333L,
                    frameBytes = byteArrayOf(0),
                    socketBufferSize = 1L shl 20,
                    operationBudget = 1L,
                    timeoutNanos = 1_000_000_000L,
                    addressFamily = 2,
                    armHoldNs = 1L,
                    armSequence = 1L,
                    maxFrame = 1L,
                    ghostPid = 1,
                    futexWaiterFlag = 0x80000000uL.toLong(),
                    disarmRequired = 0,
                    primitiveKind = Native43499PrimitiveSpec.KIND_MULTICAST_SENDMSG,
                ),
            ),
            handoff = handoff,
            runId = "single-w1-test-run",
        )
        return Flow43499Request(
            session = Flow43499Request.Session(
                init = Flow43499Request.Init(route = Flow43499Route.MulticastWaiter()),
            ),
            runSpec = runSpec,
        )
    }

    /** @return 最小的 backend victim 会话 fake（Flow 工厂接线断言用）。 */
    private fun fakeJniVictimSession(): VictimSession = object : VictimSession {
        override val kind: VictimProbeKind = VictimProbeKind.PRODUCTION
        override fun start(): VictimUidResult = VictimUidResult.Ok(0L)
        override fun uid(): VictimUidResult = VictimUidResult.Ok(2000L)
        override fun close() = Unit
    }

    private fun request(
        lifecycle: FakeLifecycle,
        verifier: Cve43499WriteVerifier,
        targetSource: Cve43499WriteTargetSource = Cve43499WriteTargetSource { context -> acquisitionFor(context) },
        policy: Cve43499WriteChainPolicy = policy(),
        handoffOverride: FinishHandoff? = null,
        finishExecutor: ((FinishHandoff, String) -> FinishExecutionEvidence)? = null,
        finishTimeoutMs: Long = 30_000L,
        victimFactory: (() -> VictimSession)? = null,
        ksuCheck: (() -> KernelsuGuardResult)? = null,
        processLimitsRaiser: (() -> ProcessLimitsOutcome)? = null,
        failStopEnabled: Boolean = false,
        failStopTerminator: ((String) -> Unit)? = null,
        failStopEvidenceSink: ((Map<String, String>) -> Unit)? = null,
    ): Flow43499Request {
        val debug = A301SoDebugConfig.parse(readDebugAsset())
        val execution = A301SoDebugConfig.executionParameters()
        val context = Cve43499GeneratorContext(debug, execution, 4242)
        val planner: Cve43499WritePlanner = Cve43499DebugWritePlanner(context)
        val chain = Cve43499WriteChain(
            targetSource = targetSource,
            planner = planner,
            policy = policy,
            primitiveToken = "multicast",
            verifier = verifier,
        )
        val handoff = handoffOverride ?: requireNotNull(
            FinishHandoff.create("/tmp/r3d-flow-test.sh", byteArrayOf(1, 2, 3)),
        )
        val runSpec = Flow43499RunSpec(
            placement = placementConfig(),
            actorSetup = actorSetup(),
            elapsedRealtimeNanos = { System.nanoTime() },
            handoff = handoff,
            runId = "r3d-test-run",
            writeChain = chain,
            writeGenerator = FakeWriteGenerator(),
            finishExecutor = finishExecutor,
            finishTimeoutMs = finishTimeoutMs,
            victimFactory = victimFactory,
            ksuCheck = ksuCheck,
            processLimitsRaiser = processLimitsRaiser,
            failStopEnabled = failStopEnabled,
            failStopTerminator = failStopTerminator,
            failStopEvidenceSink = failStopEvidenceSink,
        )
        return Flow43499Request(
            session = Flow43499Request.Session(
                init = Flow43499Request.Init(route = Flow43499Route.MulticastWaiter()),
            ),
            runSpec = runSpec,
        )
    }

    private fun policy(): Cve43499WriteChainPolicy = Cve43499WriteChainPolicy.fromDebug(
        parameters = A301SoDebugConfig.writeChainParameters(),
        primitive = DebugPrimitiveKind.MULTICAST,
    )

    private fun placementConfig(): PlacementConfig = PlacementConfig(
        placementCpu = 0,
        measurementCpu = 1,
        timeoutNanos = 60_000_000_000L,
        pageSize = 0x1000L,
        allocationSize = 0x8000L,
        slabOrder = 3L,
        mmStructSize = 0x400L,
        mmPartials = 5L,
        prepareSlabs = 8L,
        snitchCollisions = 8L,
        pollUs = 1_000L,
        requiredAlignment = 0x8000L,
        scanLowerBound = 0xffffff8000000000uL.toLong(),
        scanUpperBound = 0xffffff9000000000uL.toLong(),
        scanStep = 0x1000L,
        scanRoundLimit = 3L,
        objectsPerSlab = 32,
        prepareObjectCount = 256,
        sprayObjectCount = 192,
        preObjectCount = 31,
        postObjectCount = 32,
        identityStart = 0xffffff8000000000uL.toLong(),
        identityEnd = 0xffffff9000000000uL.toLong(),
        leakTagNibble = 0xfL,
        leakTagShift = 56L,
        stagingCapacity = 0x10000L,
        maxContextEntries = 512L,
        maxObjectsPerSlab = 32L,
        maxMeasurementRounds = 8L,
    )

    private fun actorSetup(): Native43499ActorSetup = Native43499ActorSetup(
        ownerCpu = 0,
        waiterCpu = 1,
        consumerCpu = 2,
        ownerPriority = 0,
        waiterPriority = 0,
        consumerPriority = 0,
        threadCount = 3,
        prepareTimeoutNanos = 1_000_000_000L,
        parkedTimeoutNanos = 1_000_000_000L,
        executionTimeoutNanos = 1_000_000_000L,
        shutdownTimeoutNanos = 1_000_000_000L,
        consumerMax = 1L,
        consumerBurst = 1L,
        lockPiSpinNs = 50_000_000L,
        pollUs = 1_000L,
        setupSettleUs = 50_000L,
        requeueExpected = 0L,
        disarmSpinLimit = 1L shl 20,
    )

    private fun readDebugAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-a301so.conf"),
            File("../src/debug/assets/profile/43499/debug-a301so.conf"),
            File("app/src/debug/assets/profile/43499/debug-a301so.conf"),
        )
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("debug asset not found")
    }

    private class FakeChain(
        val facts: Cve43499PlacementFacts,
        var phase: String,
    )

    /** 单测专用 Flow：注入 always-pass W3 硬门（生产默认 gate 仍由 Cve43499W3HardGate 执行）。 */
    private fun testFlow(lifecycle: Cve43499LifecycleApi): Flow43499Impl = Flow43499Impl(
        lifecycleFactory = { lifecycle },
        w3GateFactory = { AlwaysPassW3Gate() },
    )

    /** W3 硬门测试替身：本文件只验证多写链编排，不重复测 W3 gate 判据。 */
    private class AlwaysPassW3Gate : Cve43499W3HardGate() {
        override fun evaluatePre(input: PreInput): PreVerdict =
            PreVerdict(true, null, linkedMapOf("w3Gate" to "test-pass"))

        override fun evaluateFinal(
            pre: PreVerdict,
            selfStatusAfter: ByteArray?,
            parentStatusAfter: String?,
            seccompAfterParent: Int?,
            leafDirection: Boolean?,
            exactPolicy: Boolean,
            closeClean: Boolean,
            coldRebootRequired: Boolean,
        ): PreVerdict = PreVerdict(true, null, pre.evidence.toMutableMap())
    }

    private class FakeLifecycle(
        private val victimUids: ArrayDeque<Long>,
        private val routeMissOnExecuteCall: Set<Int> = emptySet(),
        private val victimSnapshotTimes: ArrayDeque<Long> = ArrayDeque(),
        private val victimSnapshotUids: ArrayDeque<Long> = ArrayDeque(),
        private val rooted: Boolean = false,
        private val victimFinishResult: VictimFinishResult =
            VictimFinishResult.Failed("victim-finish:unsupported"),
        private val closeReceipt: Cve43499LifecycleReceipt? = null,
        private val consumedDirtyOnExecuteCall: Set<Int> = emptySet(),
        private val prebuildMode: String = "disabled",
        private val closeRetentions: ArrayDeque<Boolean> = ArrayDeque(),
    ) : Cve43499LifecycleApi {
        val events: MutableList<String> = mutableListOf()
        var placements: Int = 0
        var landings: Int = 0
        var executions: Int = 0
        var closedChains: Int = 0
        var closeAllCalls: Int = 0
        var opensBeforeFirstClose: Int = 0
        var finishCalls: Int = 0
        var lastFinishScriptPath: String? = null
        var lastFinishTimeoutMs: Long? = null
        var prebuildCalls: Int = 0
        var landPrebuiltCalls: Int = 0
        var activatePrebuiltCalls: Int = 0
        var discardPrebuiltCalls: Int = 0
        var prebuiltReady: Boolean = false
        private val chains: MutableList<FakeChain> = mutableListOf()
        private var victimStarted: Boolean = false

        override fun startVictim(): String? {
            victimStarted = true
            events += "victim.start"
            return null
        }

        override fun preparePlacement(config: PlacementConfig): Cve43499PlacementStep {
            placements++
            val facts = Cve43499PlacementFacts(
                candidatePageBase = 0xffffff8800000000uL.toLong() + placements * 0x8000L,
                pageSize = 0x1000L,
                allocationSize = 0x8000L,
                requiredAlignment = 0x8000L,
                measuredObjectAddress = 0xffffff8800011234uL.toLong(),
                measurementRounds = 3L,
                retainedChildCount = 0L,
                retainedDescriptorCount = 0L,
            )
            chains += FakeChain(facts, "placement")
            events += "placement"
            return Cve43499PlacementStep.Prepared(rounds = 3L, facts = facts)
        }

        override fun landPage(config: LandingConfig): Cve43499LandingStep {
            landings++
            chains.last().phase = "page"
            events += "land"
            return Cve43499LandingStep.Landed(reclaimSends = 2L)
        }

        override fun prepareActor(preparation: ActorPreparation): Cve43499ActorPrepareStep {
            if (chains.isEmpty()) {
                chains += FakeChain(
                    Cve43499PlacementFacts(
                        candidatePageBase = 0xffffff8800020000uL.toLong(),
                        pageSize = 0x1000L,
                        allocationSize = 0x8000L,
                        requiredAlignment = 0x8000L,
                        measuredObjectAddress = 0xffffff8800021000uL.toLong(),
                        measurementRounds = 1L,
                        retainedChildCount = 0L,
                        retainedDescriptorCount = 0L,
                    ),
                    "prebuilt",
                )
            }
            chains.last().phase = "actor"
            events += "prepare-actor"
            return Cve43499ActorPrepareStep.Prepared(primitiveKind = "1")
        }

        override fun executeActor(): Cve43499ActorExecuteStep {
            executions++
            if (chains.isEmpty()) {
                chains += FakeChain(
                    Cve43499PlacementFacts(
                        candidatePageBase = 0xffffff8800020000uL.toLong(),
                        pageSize = 0x1000L,
                        allocationSize = 0x8000L,
                        requiredAlignment = 0x8000L,
                        measuredObjectAddress = 0xffffff8800021000uL.toLong(),
                        measurementRounds = 1L,
                        retainedChildCount = 0L,
                        retainedDescriptorCount = 0L,
                    ),
                    "prebuilt",
                )
            }
            chains.last().phase = "executed"
            events += "execute"
            /* 场景注入：第 N 次 execute（1-based）按脚本返回路由 clean failure。 */
            if (routeMissOnExecuteCall.contains(executions)) {
                return Cve43499ActorExecuteStep.CleanFailure("actor-execute:clean-failure")
            }
            if (consumedDirtyOnExecuteCall.contains(executions)) {
                return Cve43499ActorExecuteStep.ConsumedDirty("actor-execute:consumed-dirty")
            }
            return Cve43499ActorExecuteStep.Executed(
                Cve43499ExecuteFacts(
                    consumerSuccess = 1L,
                    consumerCalls = 1L,
                    carrierArmed = true,
                    windowOpened = true,
                    carrierOperations = 2L,
                    carrierLastStep = 0,
                    carrierLastErrno = 0,
                    retainedForProcessExit = false,
                ),
            )
        }

        override fun closeChain(): Cve43499ChainCloseReceipt {
            if (closedChains == 0) opensBeforeFirstClose = chains.size
            closedChains++
            if (chains.isNotEmpty()) chains.removeAt(chains.size - 1)
            events += "close-chain"
            val retained = closeRetentions.removeFirstOrNull() ?: false
            return Cve43499ChainCloseReceipt(
                "actor=released;page=released;placement=released;",
                allReleased = !retained,
                retained = retained,
            )
        }

        override fun prebuildRepairPlacement(config: PlacementConfig): Cve43499PlacementStep {
            prebuildCalls++
            events += "prebuild" 
            if (prebuildMode == "retained") {
                return Cve43499PlacementStep.Failed("prebuild:test-retained")
            }
            prebuiltReady = prebuildMode == "ready"
            return Cve43499PlacementStep.Prepared(
                rounds = 1L,
                facts = Cve43499PlacementFacts(
                    candidatePageBase = 0xffffff8800010000uL.toLong(),
                    pageSize = 0x1000L,
                    allocationSize = 0x8000L,
                    requiredAlignment = 0x8000L,
                    measuredObjectAddress = 0xffffff8800011000uL.toLong(),
                    measurementRounds = 1L,
                    retainedChildCount = 0L,
                    retainedDescriptorCount = 0L,
                ),
            )
        }

        override fun landPrebuildRepair(config: LandingConfig): Cve43499LandingStep {
            landPrebuiltCalls++
            events += "land-prebuild" 
            return if (prebuildMode == "ready") {
                Cve43499LandingStep.Landed(reclaimSends = 1L)
            } else {
                Cve43499LandingStep.CleanFailure("prebuild:test-clean-failure")
            }
        }

        override fun prebuiltRepairAvailable(): Boolean = prebuiltReady

        override fun activatePrebuiltRepair(): Cve43499PrebuiltActivationStep {
            activatePrebuiltCalls++
            events += "activate-prebuild" 
            if (!prebuiltReady) {
                return Cve43499PrebuiltActivationStep.Failed("prebuild:not-ready", false)
            }
            prebuiltReady = false
            return Cve43499PrebuiltActivationStep.Activated(prebuiltNanos = 0L)
        }

        override fun discardPrebuiltRepair(): Cve43499PrebuiltDiscardReceipt {
            discardPrebuiltCalls++
            val retained = prebuildMode == "retained"
            prebuiltReady = false
            return Cve43499PrebuiltDiscardReceipt(
                clean = !retained,
                retained = retained,
                termination = "prebuilt=test-discarded;",
            )
        }

        override fun closeAll(): Cve43499LifecycleReceipt {
            closeAllCalls++
            chains.clear()
            return closeReceipt ?: Cve43499LifecycleReceipt("victim=closed;", true, false)
        }

        override fun victimRooted(): Boolean = rooted

        override fun victimFinish(scriptPath: String, timeoutMs: Long): VictimFinishResult {
            finishCalls++
            lastFinishScriptPath = scriptPath
            lastFinishTimeoutMs = timeoutMs
            return victimFinishResult
        }

        override fun victimSnapshot(): Cve43499VictimSnapshot = Cve43499VictimSnapshot(
            pid = 1234,
            startTimeTicks = victimSnapshotTimes.removeFirstOrNull() ?: 777L,
            uid = victimSnapshotUids.removeFirstOrNull()
                ?: if (victimStarted) (victimUids.firstOrNull() ?: 0L) else 1L,
        )

        override fun victimUid(): Cve43499UidProbe {
            val uid = victimUids.removeFirstOrNull()
            return if (uid == null) {
                Cve43499UidProbe(null, "victim-probe:epipe")
            } else {
                Cve43499UidProbe(uid, null)
            }
        }

    }

    private class FakeWriteGenerator : Cve43499WriteGenerator {
        override fun generateForWrite(
            facts: Cve43499PlacementFacts,
            request: Cve43499WriteRequest,
        ): Cve43499GeneratedInputs = Cve43499GeneratedInputs(
            landing = LandingConfig(
                finalBytes = ByteArray(0x10000),
                contentGeneration = 1L,
                timeoutNanos = 1_000_000_000L,
                socketSendBufferSize = 1L shl 20,
                reclaimSendLimit = 4L,
                reclaimPayloadSize = 0x10000L,
                fillByte = 0x41L,
                pageAcceptRequired = if (request.preserveChild) 1L else 0L,
                pageAcceptOffset = if (request.preserveChild) 0x100L else 0L,
                pageAcceptBit = if (request.preserveChild) 16L else 0L,
            ),
            primitive = dummyPrimitive(),
        )

        private fun dummyPrimitive(): Native43499PrimitiveSpec = Native43499MulticastSpec(
            finalWaiterAddress = 0x1111L,
            finalTaskAddress = 0x2222L,
            finalLockAddress = 0x3333L,
            frameBytes = byteArrayOf(0),
            socketBufferSize = 1L shl 20,
            operationBudget = 1L,
            timeoutNanos = 1_000_000_000L,
            addressFamily = 2,
            armHoldNs = 1L,
            armSequence = 1L,
            maxFrame = 1L,
            ghostPid = 1,
            futexWaiterFlag = 0x80000000uL.toLong(),
            disarmRequired = 0,
            primitiveKind = Native43499PrimitiveSpec.KIND_MULTICAST_SENDMSG,
        )
    }

    private class FakeVerifier(
        private val w1Verdicts: ArrayDeque<Cve43499WriteVerifyVerdict> =
            ArrayDeque(listOf(Cve43499WriteVerifyVerdict.VERIFIED)),
        private val leafDirections: ArrayDeque<Cve43499LeafDirection> = ArrayDeque(),
        private val w3Verdicts: ArrayDeque<Cve43499WriteVerifyVerdict> = ArrayDeque(),
        private val unsupportedReason: String = "verifier-unsupported",
    ) : Cve43499WriteVerifier {
        override fun verifyW1(): Cve43499WriteVerifyVerdict =
            w1Verdicts.removeFirstOrNull() ?: Cve43499WriteVerifyVerdict.VERIFIED

        override fun w1UnsupportedReason(): String = unsupportedReason

        override fun leafDirection(): Cve43499LeafDirection? = leafDirections.removeFirstOrNull()

        override fun verifyW3(): Cve43499WriteVerifyVerdict =
            w3Verdicts.removeFirstOrNull() ?: Cve43499WriteVerifyVerdict.VERIFIED
    }

    private companion object {
        private val VICTIM_TASK = 0xffffff8800100000uL.toLong()
    }
}
