package com.ghostlock.app.backend.backends.cve43499.perf

import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499TargetAcquisition
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499VictimBaseline
import com.ghostlock.app.backend.backends.cve43499.flow.Cve43499VictimContext
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.data.cve43499.DebugExecutionParameters
import com.ghostlock.app.data.cve43499.DebugPerfParameters
import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499PerfCollectResult
import com.ghostlock.app.jni.Native43499PerfOpenRequest
import com.ghostlock.app.jni.Native43499PerfOpenResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * perf 目标源单测（R3e-2 方案 B：2-of-3 + 同意 pass 聚合门槛 + 判定顺序）。
 *
 * @note 纯 JVM：fake Native 逐 pass 喂入合成的 ring；pump 用 lambda。
 */
class Cve43499PerfTargetSourceTest {

    @Test
    fun `two agreeing passes acquire with aggregated threshold`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 200, candidates = 256),
                ringScript(TASK, votes = 200, candidates = 256),
            ),
        )
        val result = acquire(native)
        val acquired = result as Cve43499TargetAcquisition.Acquired
        assertEquals(TASK, acquired.victimTaskAddress)
        assertEquals(2, acquired.evidence.passes)
        assertEquals(400L, acquired.evidence.votes)
        assertEquals(512L, acquired.evidence.candidates)
        assertEquals(128L, acquired.evidence.threshold)
        assertEquals(2, native.openCalls)
        assertEquals(2, native.closeCalls)
    }

    @Test
    fun `10-06 sequence low-vote different address does not abort`() {
        val other = TASK + 0x200L
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 149, candidates = 256),
                ringScript(other, votes = 34, candidates = 256),
                ringScript(TASK, votes = 162, candidates = 256),
            ),
        )
        val acquired = acquire(native) as Cve43499TargetAcquisition.Acquired
        assertEquals(TASK, acquired.victimTaskAddress)
        assertEquals(3, acquired.evidence.passes)
        assertEquals(311L, acquired.evidence.votes)
        assertEquals(512L, acquired.evidence.candidates)
        assertEquals(128L, acquired.evidence.threshold)
        assertEquals(3, native.openCalls)
    }

    @Test
    fun `all three distinct fails agreement`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 100, candidates = 256),
                ringScript(TASK + 0x200L, votes = 100, candidates = 256),
                ringScript(TASK + 0x400L, votes = 100, candidates = 256),
            ),
        )
        assertMissing(acquire(native), "perf-agreement-failed")
        assertEquals(3, native.openCalls)
    }

    @Test
    fun `64 plus 64 votes pass but 63 plus 63 fail`() {
        val passing = acquire(
            FakePerfNative(
                listOf(
                    ringScript(TASK, votes = 64, candidates = 256),
                    ringScript(TASK, votes = 64, candidates = 256),
                ),
            ),
        )
        assertTrue(passing is Cve43499TargetAcquisition.Acquired)
        val failing = acquire(
            FakePerfNative(
                listOf(
                    ringScript(TASK, votes = 63, candidates = 256),
                    ringScript(TASK, votes = 63, candidates = 256),
                ),
            ),
        )
        assertMissing(failing, "perf-votes-below-threshold")
    }

    @Test
    fun `candidates below floor fail insufficient before votes`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 31, candidates = 31),
                ringScript(TASK, votes = 31, candidates = 31),
            ),
        )
        assertMissing(acquire(native), "perf-insufficient-candidates")
    }

    @Test
    fun `open denied maps to named reason and aborts`() {
        val native = FakePerfNative(emptyList(), openErrno = 13)
        assertMissing(acquire(native), "perf-open-denied")
        assertEquals(1, native.openCalls)
        assertEquals(0, native.closeCalls)
    }

    @Test
    fun `parser failure aborts immediately without extra passes`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 200, candidates = 256),
                RingScript(ByteArray(RING_BYTES.toInt()), head = 0L, tail = 0L),
            ),
        )
        assertMissing(acquire(native), "perf-no-samples")
        assertEquals(2, native.openCalls)
        assertEquals(2, native.closeCalls)
    }

    @Test
    fun `pump failure closes handle and aborts`() {
        val native = FakePerfNative(listOf(ringScript(TASK, votes = 200, candidates = 256)))
        val result = acquire(native, pump = { _, _ -> "victim-probe:epipe" })
        assertMissing(result, "perf-pump-failed:victim-probe:epipe")
        assertEquals(1, native.openCalls)
        assertEquals(1, native.closeCalls)
    }

    @Test
    fun `close retained is evidence only`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 200, candidates = 256),
                ringScript(TASK, votes = 200, candidates = 256),
            ),
            closeRetained = true,
        )
        val acquired = acquire(native) as Cve43499TargetAcquisition.Acquired
        assertTrue(acquired.evidence.closeRetained)
    }

    @Test
    fun `require alignment turns evidence into hard failure`() {
        val misaligned = TASK + 8L
        val scripts = listOf(
            ringScript(misaligned, votes = 200, candidates = 256),
            ringScript(misaligned, votes = 200, candidates = 256),
        )
        val lenient = acquire(FakePerfNative(scripts)) as Cve43499TargetAcquisition.Acquired
        assertEquals(false, lenient.evidence.alignmentOk)
        val strict = acquire(
            FakePerfNative(scripts),
            perf = perfParameters(requireAlignment = 1),
        )
        assertMissing(strict, "perf-address-untrusted:alignment")
    }

    @Test
    fun `weight bit sample type is rejected as config-invalid`() {
        val native = FakePerfNative(emptyList())
        val weightOnly = 1L shl 14
        assertMissing(
            acquire(native, perf = perfParameters(sampleType = weightOnly)),
            "perf-config-invalid",
        )
        assertEquals(0, native.openCalls)
    }

    @Test
    fun `failure path carries close retained fact`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 200, candidates = 256),
                RingScript(ByteArray(RING_BYTES.toInt()), head = 0L, tail = 0L),
            ),
            closeRetained = true,
        )
        val missing = acquire(native) as Cve43499TargetAcquisition.Missing
        assertEquals("perf-no-samples", missing.reason)
        assertTrue(missing.closeRetained)
    }

    @Test
    fun `abi2 records are accepted through target source`() {
        val native = FakePerfNative(
            listOf(
                ringScript(TASK, votes = 200, candidates = 256, abi = 2L),
                ringScript(TASK, votes = 200, candidates = 256, abi = 2L),
            ),
        )
        val acquired = acquire(native) as Cve43499TargetAcquisition.Acquired
        assertEquals(TASK, acquired.victimTaskAddress)
        assertEquals(400L, acquired.evidence.votes)
        assertEquals(512L, acquired.evidence.candidates)
    }

    @Test
    fun `disabled perf fails closed before any native call`() {
        val native = FakePerfNative(emptyList())
        assertMissing(acquire(native, perf = perfParameters(enabled = 0)), "perf-config-invalid")
        assertEquals(0, native.openCalls)
    }

    private fun acquire(
        native: FakePerfNative,
        perf: DebugPerfParameters = perfParameters(),
        pump: (Long, Long) -> String? = { _, _ -> null },
    ): Cve43499TargetAcquisition {
        val source = Cve43499PerfTargetSource(
            native = native,
            perf = perf,
            execution = A301SoDebugConfig.executionParameters(),
        )
        return source.acquire(
            Cve43499VictimContext(
                baseline = Cve43499VictimBaseline(
                    pid = 1234,
                    startTimeTicks = 777L,
                    uidProbe = 2000L,
                ),
                pump = pump,
            ),
        )
    }

    private fun assertMissing(
        result: Cve43499TargetAcquisition,
        reason: String,
    ) {
        val missing = result as? Cve43499TargetAcquisition.Missing
        assertTrue("expected Missing, got $result", missing != null)
        assertEquals(reason, missing?.reason)
    }

    private fun ringScript(
        address: Long,
        votes: Int,
        candidates: Int,
        abi: Long = 1L,
    ): RingScript {
        require(votes in 1..candidates)
        require(candidates <= MAX_CANDIDATES)
        val ring = TestRing(RING_BYTES)
        var position = 0L
        repeat(candidates) { index ->
            val value = if (index < votes) {
                address
            } else {
                address + 0x200L * (index - votes + 1)
            }
            ring.writeSample(position, abi, TaskRegisters.of(value))
            position += 280L
        }
        return RingScript(ring.bytes, head = position, tail = 0L)
    }

    private fun perfParameters(
        enabled: Int = 1,
        requireAlignment: Int = 0,
        requireSpEvidence: Int = 0,
        sampleType: Long = 262145L,
        attrFlags: Long = 8L,
    ): DebugPerfParameters = DebugPerfParameters(
        enabled = enabled,
        attrType = 1L,
        attrConfig = 0L,
        attrSamplePeriod = 5000L,
        attrSampleType = sampleType,
        attrSampleRegsIntr = 0xFFFFFFFFL,
        attrFlags = attrFlags,
        attrDisabled = 1L,
        excludeUser = 1L,
        excludeHv = 1L,
        excludeIdle = 1L,
        autoEnable = 1L,
        ringPages = 64L,
        pumpIterations = 8L,
        pumpBudgetNanos = 0L,
        maxPasses = 3,
        minVotesFloor = 64L,
        minVotesRatio = 0.25,
        taskAlignment = 0x200L,
        registerCount = 32,
        spRegisterIndex = 31,
        candidateCapacity = 256,
        requireAlignment = requireAlignment,
        requireSpEvidence = requireSpEvidence,
        settleAfterAcquireUs = 0L,
    )

    private companion object {
        val RING_BYTES: Long =
            A301SoDebugConfig.executionParameters().pageSize * 64L
        const val MAX_CANDIDATES = 256
        val TASK = 0xffffff896fe31200uL.toLong()
    }
}

/** fake Native 的采集脚本：环副本 + 绝对游标。 */
internal data class RingScript(
    val bytes: ByteArray,
    val head: Long,
    val tail: Long,
    val dataSize: Long = bytes.size.toLong(),
)

/** 纯 JVM fake Native：逐 pass 返回脚本化 ring；统计调用次数（owner 契约用）。 */
internal class FakePerfNative(
    private val scripts: List<RingScript>,
    private val openErrno: Int? = null,
    private val closeRetained: Boolean = false,
) : Cve43499PerfNative {
    var openCalls: Int = 0
    var collectCalls: Int = 0
    var closeCalls: Int = 0
    private var nextHandle: Long = 100L

    override fun open(
        request: Native43499PerfOpenRequest,
        result: Native43499PerfOpenResult,
    ): Long {
        openCalls++
        if (openErrno != null) {
            result.callStatus = 8
            result.systemError = openErrno
            return 0L
        }
        result.callStatus = 0
        return nextHandle++
    }

    override fun collect(
        handle: Long,
        out: ByteArray,
        result: Native43499PerfCollectResult,
    ): Int {
        val script = scripts.getOrNull(collectCalls)
            ?: RingScript(ByteArray(out.size), head = 0L, tail = 0L)
        collectCalls++
        script.bytes.copyInto(out, 0, 0, minOf(out.size, script.bytes.size))
        result.callStatus = 0
        result.dataSize = script.dataSize
        result.dataHead = script.head
        result.dataTailBefore = script.tail
        result.bytesCopied = script.dataSize
        result.tailMoved = true
        return 0
    }

    override fun close(handle: Long, result: Native43499CloseResult): Int {
        closeCalls++
        result.callStatus = 0
        result.retainedForProcessExit = closeRetained
        return 0
    }
}
