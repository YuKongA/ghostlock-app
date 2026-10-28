package com.ghostlock.app.backend.cve43499.victim

import com.ghostlock.app.backend.cve43499.flow.Cve43499TargetAcquisition
import com.ghostlock.app.backend.cve43499.flow.Cve43499VictimBaseline
import com.ghostlock.app.backend.cve43499.flow.Cve43499VictimContext
import com.ghostlock.app.backend.cve43499.perf.TaskRegisters
import com.ghostlock.app.backend.cve43499.perf.TestRing
import com.ghostlock.app.core.victim.VictimError
import com.ghostlock.app.core.victim.VictimNative
import com.ghostlock.app.core.victim.VictimPerfSample
import com.ghostlock.app.core.victim.VictimProtocol
import com.ghostlock.app.core.victim.VictimResult
import com.ghostlock.app.core.victim.VictimSpawnRequest
import com.ghostlock.app.core.victim.VictimSpawned
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.data.cve43499.DebugPerfParameters
import java.io.File
import java.util.ArrayDeque
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * R4/批 B2 单测：`Cve43499VictimAdapter`（通用 victim → backend VictimSession 适配）
 * 与 `Cve43499JniVictimTargetSource`（helper self-perf → 解析/投票/2-of-3）。
 *
 * @note 纯 JVM；JNI/helper/真机 perf 未验证（E4/E5）。fake `VictimNative` 驱动适配器，
 *   fake sampler + [TestRing] 驱动 target source（parser 复用既有 R3e 向量生成器）。
 */
class Cve43499JniVictimTest {

    /* ------------------------------------------------------------------ */
    /* Adapter：通用 victim → backend VictimSession                        */
    /* ------------------------------------------------------------------ */

    @Test
    fun `adapter starts helper once and probes uid`() {
        val native = FakeNative()
        native.receiveQueue.add(VictimResult.Ok(probeReply(10_123L)))
        val runtime = runtime(native)
        val session = runtime.newVictimSession()
        assertEquals(VictimUidResult.Ok(0L), session.start())
        assertEquals(VictimUidResult.Ok(0L), session.start())
        assertEquals(1, native.spawns.size)
        assertEquals("/lib/libghostlock_victim_helper.so", native.spawns.single().helperPath)
        assertEquals(VictimUidResult.Ok(10_123L), session.uid())
        assertFalse(session.rooted)
        assertEquals(VictimProbeKind.PRODUCTION, session.kind)
        session.close()
    }

    @Test
    fun `adapter marks rooted when uid is zero and detached without kill`() {
        val native = FakeNative()
        native.receiveQueue.add(VictimResult.Ok(probeReply(0L)))
        native.receiveQueue.add(VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)))
        val runtime = runtime(native)
        val session = runtime.newVictimSession()
        assertEquals(VictimUidResult.Ok(0L), session.start())
        assertEquals(VictimUidResult.Ok(0L), session.uid())
        assertTrue(session.rooted)
        assertTrue(session.detachRooted())
        assertEquals(1, native.detachCalls)
        assertEquals(0, native.killCalls)
        assertEquals(0, native.closeCalls)
        assertNull(runtime.currentSampler())
    }

    @Test
    fun `adapter finish maps helper execScript exit code and output digest`() {
        val output = byteArrayOf(0x4f, 0x4b, 0x0a)
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(
                execReply(exitCode = 7, outputLen = output.size.toLong(), timedOut = false),
            ),
        )
        native.raw = VictimProtocol.encodeRawFrame(output)
        val runtime = runtime(native)
        val session = runtime.newVictimSession()
        session.start()
        val finish = session.finish("/data/user/0/app/files/finish/run.sh", 30_000L)
        assertTrue(finish is VictimFinishResult.Ok)
        val ok = finish as VictimFinishResult.Ok
        assertEquals(7, ok.exitCode)
        assertEquals(VictimProtocol.sha256Hex(output), ok.outputSha256)
        assertFalse(ok.timedOut)
        session.close()
    }

    @Test
    fun `adapter finish maps timed out and helper errors`() {
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(
                execReply(exitCode = 137, outputLen = 0L, timedOut = true),
            ),
        )
        native.raw = VictimProtocol.encodeRawFrame(ByteArray(0))
        val runtime = runtime(native)
        val session = runtime.newVictimSession()
        session.start()
        val timedOut = session.finish("/data/run.sh", 1_000L)
        assertTrue(timedOut is VictimFinishResult.Ok)
        assertTrue((timedOut as VictimFinishResult.Ok).timedOut)
        assertEquals(137, timedOut.exitCode)
        session.close()

        val errorNative = FakeNative()
        errorNative.receiveQueue.add(
            VictimResult.Ok(
                VictimProtocol.encodeReply(
                    VictimProtocol.Status.ERROR,
                    errorCode = VictimProtocol.ErrorCode.EXEC,
                    errorErrno = 2,
                ),
            ),
        )
        val errorSession = runtime(errorNative).newVictimSession()
        errorSession.start()
        val failed = errorSession.finish("/data/run.sh", 1_000L)
        assertTrue(failed is VictimFinishResult.Failed)
        assertTrue(
            (failed as VictimFinishResult.Failed).reason.contains("helper-error"),
        )
        errorSession.close()
    }

    @Test
    fun `adapter rejects truncated finish output`() {
        val output = byteArrayOf(1, 2, 3)
        val native = FakeNative()
        native.receiveQueue.add(
            VictimResult.Ok(
                execReply(exitCode = 0, outputLen = output.size.toLong(), timedOut = false, truncated = true),
            ),
        )
        native.raw = VictimProtocol.encodeRawFrame(output, truncated = true)
        val session = runtime(native).newVictimSession()
        session.start()
        val finish = session.finish("/data/run.sh", 30_000L)
        assertTrue(finish is VictimFinishResult.Failed)
        assertEquals(
            "victim-finish:output-truncated",
            (finish as VictimFinishResult.Failed).reason,
        )
        session.close()
    }

    @Test
    fun `adapter close sends bye and closes native handle once`() {
        val native = FakeNative()
        native.receiveQueue.add(VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)))
        val runtime = runtime(native)
        val session = runtime.newVictimSession()
        session.start()
        session.close()
        session.close()
        assertEquals(1, native.waitCalls)
        assertEquals(1, native.closeCalls)
        assertEquals(0, native.killCalls)
        assertTrue(
            native.sent.any {
                (VictimProtocol.decodeCommand(it) as VictimResult.Ok).value.opcode ==
                    VictimProtocol.Opcode.BYE
            },
        )
    }

    @Test
    fun `runtime replaces prior session and dispose is idempotent`() {
        val native = FakeNative()
        native.receiveQueue.add(VictimResult.Ok(VictimProtocol.encodeReply(VictimProtocol.Status.OK)))
        val runtime = runtime(native)
        val first = runtime.newVictimSession()
        first.start()
        val second = runtime.newVictimSession()
        /* 旧会话被静默收口（best-effort bye + close）。 */
        assertTrue(native.closeCalls >= 1)
        assertNull(runtime.currentSampler())
        second.start()
        runtime.dispose()
        runtime.dispose()
        assertTrue(native.closeCalls >= 2)
    }

    /* ------------------------------------------------------------------ */
    /* Target source：helper self-perf → 解析/投票/2-of-3                   */
    /* ------------------------------------------------------------------ */

    @Test
    fun `target source accepts abi2 helper samples and returns trusted evidence`() {
        val perf = debugPerf()
        val ring = TestRing(ringBytes())
        ring.writeSample(tail = 0L, abi = ABI_32, registers = TaskRegisters.of(TASK))
        val sampler = samplerOf(sample(ring), sample(ring))
        val source = Cve43499JniVictimTargetSource(
            samplerProvider = { sampler },
            perf = perf,
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { true },
        )
        val acquisition = source.acquire(context())
        assertTrue(acquisition is Cve43499TargetAcquisition.Acquired)
        val acquired = acquisition as Cve43499TargetAcquisition.Acquired
        assertEquals(TASK, acquired.victimTaskAddress)
        assertEquals(2, acquired.evidence.passes)
        assertEquals(listOf(TASK, TASK), acquired.evidence.passAddresses)
        assertTrue(acquired.evidence.trusted())
        assertEquals(2, sampler.requests.size)
        assertEquals(
            ringBytes() + VictimProtocol.RAW_HEADER_BYTES,
            sampler.requests.first().rawCapacity,
        )
        assertEquals(0L, sampler.requests.first().rawOffset)
    }

    @Test
    fun `target source rejects unknown abi with named record invalid`() {
        val ring = TestRing(ringBytes())
        ring.writeSample(tail = 0L, abi = 3L, registers = TaskRegisters.of(TASK))
        val source = Cve43499JniVictimTargetSource(
            samplerProvider = { samplerOf(sample(ring)) },
            perf = debugPerf(),
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { true },
        )
        val acquisition = source.acquire(context())
        assertTrue(acquisition is Cve43499TargetAcquisition.Missing)
        assertEquals(
            "perf-record-invalid",
            (acquisition as Cve43499TargetAcquisition.Missing).reason,
        )
    }

    @Test
    fun `target source fails closed without w1 permissive`() {
        val source = Cve43499JniVictimTargetSource(
            samplerProvider = { FakeSampler(emptyList()) },
            perf = debugPerf(),
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { false },
        )
        val acquisition = source.acquire(context())
        assertTrue(acquisition is Cve43499TargetAcquisition.Missing)
        assertEquals(
            "perf-permissive-required",
            (acquisition as Cve43499TargetAcquisition.Missing).reason,
        )
    }

    @Test
    fun `target source fails closed when victim is not started`() {
        val source = Cve43499JniVictimTargetSource(
            samplerProvider = { null },
            perf = debugPerf(),
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { true },
        )
        val acquisition = source.acquire(context())
        assertTrue(acquisition is Cve43499TargetAcquisition.Missing)
        assertEquals(
            "perf-victim-not-started",
            (acquisition as Cve43499TargetAcquisition.Missing).reason,
        )
    }

    @Test
    fun `target source maps helper timeout and truncated samples to named failure`() {
        val timeout = Cve43499JniVictimTargetSource(
            samplerProvider = { FakeSampler(listOf(VictimResult.failed(VictimError.TIMEOUT.token))) },
            perf = debugPerf(),
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { true },
        )
        val timeoutAcquisition = timeout.acquire(context())
        assertTrue(timeoutAcquisition is Cve43499TargetAcquisition.Missing)
        assertEquals(
            "perf-victim:" + VictimError.TIMEOUT.token,
            (timeoutAcquisition as Cve43499TargetAcquisition.Missing).reason,
        )

        val ring = TestRing(ringBytes())
        ring.writeSample(tail = 0L, abi = ABI_64, registers = TaskRegisters.of(TASK))
        val truncated = Cve43499JniVictimTargetSource(
            samplerProvider = { samplerOf(sample(ring).copy(truncated = true)) },
            perf = debugPerf(),
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { true },
        )
        val truncatedAcquisition = truncated.acquire(context())
        assertTrue(truncatedAcquisition is Cve43499TargetAcquisition.Missing)
        assertTrue(
            (truncatedAcquisition as Cve43499TargetAcquisition.Missing)
                .reason.startsWith("perf-victim:helper:errno=0:truncated=true"),
        )
    }

    @Test
    fun `target source reports agreement failure for three distinct addresses`() {
        val addresses = listOf(TASK, TASK + 0x1000L, TASK + 0x2000L)
        val sampler = samplerOf(
            *addresses.map { address ->
                val ring = TestRing(ringBytes())
                ring.writeSample(tail = 0L, abi = ABI_64, registers = TaskRegisters.of(address))
                sample(ring)
            }.toTypedArray(),
        )
        val source = Cve43499JniVictimTargetSource(
            samplerProvider = { sampler },
            perf = debugPerf(),
            execution = A301SoDebugConfig.executionParameters(),
            permissiveProbe = { true },
        )
        val acquisition = source.acquire(context())
        assertTrue(acquisition is Cve43499TargetAcquisition.Missing)
        assertEquals(
            "perf-agreement-failed",
            (acquisition as Cve43499TargetAcquisition.Missing).reason,
        )
        assertEquals(3, sampler.requests.size)
    }

    /* ------------------------------------------------------------------ */
    /* 夹具                                                                 */
    /* ------------------------------------------------------------------ */

    /** @return 每次采样环容量（pageSize × ring_pages）。 */
    private fun ringBytes(): Long =
        A301SoDebugConfig.executionParameters().pageSize * debugPerf().ringPages

    /** @return debug perf 参数（enabled=1；测试阈值放宽以匹配小样本）。 */
    private fun debugPerf(): DebugPerfParameters {
        val parsed = A301SoDebugConfig.parse(readDebugAsset())
        return requireNotNull(parsed.perf).copy(
            enabled = 1,
            minVotesFloor = 1L,
            minVotesRatio = 0.25,
            maxPasses = 3,
            settleAfterAcquireUs = 0L,
        )
    }

    /** @return 采样上下文（baseline 与 evidence 对齐）。 */
    private fun context(): Cve43499VictimContext = Cve43499VictimContext(
        baseline = Cve43499VictimBaseline(pid = 4242, startTimeTicks = 1234L, uidProbe = 10_123L),
        pump = { _, _ -> null },
    )

    /** @return 一次成功采样（整环副本 + head=280）。 */
    private fun sample(ring: TestRing): VictimPerfSample = VictimPerfSample(
        rawBytes = ring.bytes.copyOf(),
        rawSha256 = VictimProtocol.sha256Hex(ring.bytes),
        dataHead = 280L,
        helperErrno = 0,
        truncated = false,
    )

    /** @return 测试 runtime（helper 路径不用真 JNI）。 */
    private fun runtime(native: FakeNative): Cve43499JniVictimRuntime =
        Cve43499JniVictimRuntime(
            helperPath = "/lib/libghostlock_victim_helper.so",
            rawSizeBytes = 1_040L,
            native = native,
        )

    /** @return probeUid OK 回复线帧（u32 LE）。 */
    private fun probeReply(uid: Long): ByteArray {
        val data = ByteArray(4)
        for (index in 0 until 4) data[index] = ((uid ushr (8 * index)) and 0xFF).toByte()
        return VictimProtocol.encodeReply(VictimProtocol.Status.OK, data)
    }

    /** @return execScript OK 回复线帧（24B 数据区）。 */
    private fun execReply(
        exitCode: Int,
        outputLen: Long,
        timedOut: Boolean,
        truncated: Boolean = false,
    ): ByteArray {
        val data = ByteArray(24)
        putInt(data, 0, exitCode)
        putLong(data, 4, outputLen)
        putInt(data, 12, if (truncated) 1 else 0)
        putInt(data, 16, if (timedOut) 1 else 0)
        putInt(data, 20, 0)
        return VictimProtocol.encodeReply(VictimProtocol.Status.OK, data)
    }

    /** @param bytes 目标。 @param offset 偏移。 @param value 值（LE 4B）。 */
    private fun putInt(bytes: ByteArray, offset: Int, value: Int) {
        for (index in 0 until 4) {
            bytes[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    /** @param bytes 目标。 @param offset 偏移。 @param value 值（LE 8B）。 */
    private fun putLong(bytes: ByteArray, offset: Int, value: Long) {
        for (index in 0 until 8) {
            bytes[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    /** @return debug 资产文本（repo 根解析）。 */
    private fun readDebugAsset(): String {
        val candidates = listOf(
            File("src/debug/assets/profile/43499/debug-a301so.conf"),
            File("../src/debug/assets/profile/43499/debug-a301so.conf"),
            File("app/src/debug/assets/profile/43499/debug-a301so.conf"),
        )
        return candidates.firstOrNull { it.isFile }?.readText()
            ?: error("debug asset not found from ${File(".").absolutePath}")
    }

    /** 脚本化 [VictimNative] fake（只按队列回帧；不解析命令语义）。 */
    private class FakeNative : VictimNative {
        val spawns: MutableList<VictimSpawnRequest> = mutableListOf()
        val sent: MutableList<ByteArray> = mutableListOf()
        val receiveQueue: ArrayDeque<VictimResult<ByteArray>> = ArrayDeque()
        val waits: ArrayDeque<VictimResult<Int>> = ArrayDeque()
        var spawnResult: VictimResult<VictimSpawned> =
            VictimResult.Ok(VictimSpawned(handle = 7L, pid = 4_242))
        var raw: ByteArray = ByteArray(0)
        var waitCalls: Int = 0
        var killCalls: Int = 0
        var detachCalls: Int = 0
        var closeCalls: Int = 0

        override fun spawn(request: VictimSpawnRequest): VictimResult<VictimSpawned> {
            spawns += request
            return spawnResult
        }

        override fun send(
            handle: Long,
            frame: ByteArray,
            timeoutMs: Long,
        ): VictimResult<Unit> {
            sent += frame
            return VictimResult.Ok(Unit)
        }

        override fun receive(
            handle: Long,
            capacity: Int,
            timeoutMs: Long,
        ): VictimResult<ByteArray> =
            receiveQueue.pollFirst() ?: VictimResult.failed(VictimError.TIMEOUT.token)

        override fun readRaw(handle: Long, offset: Long, length: Long): VictimResult<ByteArray> {
            val end = offset + length
            if (offset < 0L || length <= 0L || end > raw.size.toLong()) {
                return VictimResult.failed(VictimError.RAW_READ_FAILED.token)
            }
            return VictimResult.Ok(raw.copyOfRange(offset.toInt(), end.toInt()))
        }

        override fun rawSize(handle: Long): VictimResult<Long> = VictimResult.Ok(raw.size.toLong())

        override fun waitExit(handle: Long, timeoutMs: Long): VictimResult<Int> {
            waitCalls++
            return waits.pollFirst() ?: VictimResult.Ok(0)
        }

        override fun kill(handle: Long): VictimResult<Int> {
            killCalls++
            return VictimResult.Ok(137)
        }

        override fun detach(handle: Long) {
            detachCalls++
        }

        override fun close(handle: Long) {
            closeCalls++
        }
    }

    /** @param initial 预置采样结局。 @return 采样器 fake。 */
    private fun samplerOf(vararg samples: VictimPerfSample): FakeSampler =
        FakeSampler(samples.map { VictimResult.Ok(it) })

    /** 脚本化 self-perf sampler fake（按队列返回结局）。 */
    private class FakeSampler(
        initial: List<VictimResult<VictimPerfSample>>,
    ) : Cve43499VictimPerfSampler {
        val queue: ArrayDeque<VictimResult<VictimPerfSample>> = ArrayDeque(initial)
        val requests: MutableList<VictimProtocol.PerfSampleRequest> = mutableListOf()

        override fun perfSelfSample(
            request: VictimProtocol.PerfSampleRequest,
        ): VictimResult<VictimPerfSample> {
            requests += request
            return queue.pollFirst() ?: VictimResult.failed(VictimError.TIMEOUT.token)
        }
    }

    private companion object {
        /** ABI=64 payload（候选样本）。 */
        const val ABI_64 = 1L

        /** ABI=32 payload（A301SO 内核实际投递格式；R3e G1 证据）。 */
        const val ABI_32 = 2L

        /** 测试目标 task_struct 地址（身份窗内）。 */
        val TASK: Long = 0xffffff896fe31200uL.toLong()
    }
}
