package com.ghostlock.app.backend.cve43499.perf

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * perf record 解析器单测（R3e-2）：280/24、272/279/281 invalid、绝对 head/tail、
 * 回绕/跨环、tag 还原、投票平票、开区间边界、SP 证据。
 *
 * @note 纯 JVM；不触 Native。环由 [TestRing] 逐字节合成（与内核写 record 同形）。
 */
class Cve43499PerfSampleParserTest {

    @Test
    fun `280-byte abi64 sample yields expected winner with tag restore`() {
        val ring = TestRing(DATA_SIZE)
        ring.writeSample(tail = 0L, abi = ABI_64, registers = TaskRegisters.of(TASK))
        val result = parse(ring, head = 280L, tail = 0L)
        val address = result as Cve43499PerfPass.Address
        assertEquals(TASK, address.address)
        assertEquals(1L, address.votes)
        assertEquals(1L, address.candidates)
        assertEquals(1L, address.spVotes)
        assertTrue(address.alignmentOk)
        assertTrue(address.spEvidenceOk)
    }

    @Test
    fun `24-byte abi-none record is a named registers-not-delivered`() {
        val ring = TestRing(DATA_SIZE)
        ring.writeAbiNoneRecord(tail = 0L)
        val result = parse(ring, head = 24L, tail = 0L)
        assertEquals(
            Cve43499PerfPass.Missing("perf-registers-not-delivered"),
            result,
        )
    }

    @Test
    fun `272 279 281 byte abi64 records are invalid`() {
        for (size in listOf(272L, 279L, 281L)) {
            val ring = TestRing(DATA_SIZE)
            ring.writeSample(tail = 0L, abi = ABI_64, registers = TaskRegisters.of(TASK))
            ring.putU16(6L, size)
            val result = parse(ring, head = size, tail = 0L)
            assertEquals(Cve43499PerfPass.Missing("perf-record-invalid"), result)
        }
    }

    @Test
    fun `monotonic head greater than data size is a normal wrap`() {
        val ring = TestRing(DATA_SIZE)
        val tail = 4100L
        ring.writeSample(tail = tail, abi = ABI_64, registers = TaskRegisters.of(TASK))
        val result = parse(ring, head = tail + 280L, tail = tail)
        assertEquals(TASK, (result as Cve43499PerfPass.Address).address)
    }

    @Test
    fun `unconsumed over ring size is a named overflow`() {
        val ring = TestRing(DATA_SIZE)
        val result = parse(ring, head = DATA_SIZE + 1L, tail = 0L)
        assertEquals(Cve43499PerfPass.Missing("perf-ring-overflow"), result)
    }

    @Test
    fun `record straddling ring end is assembled with modulo`() {
        val ring = TestRing(DATA_SIZE)
        val tail = DATA_SIZE - 100L
        ring.writeSample(tail = tail, abi = ABI_64, registers = TaskRegisters.of(TASK))
        val result = parse(ring, head = tail + 280L, tail = tail)
        assertEquals(TASK, (result as Cve43499PerfPass.Address).address)
    }

    @Test
    fun `open interval boundaries are rejected`() {
        val ring = TestRing(DATA_SIZE)
        ring.writeSample(
            tail = 0L,
            abi = ABI_64,
            registers = TaskRegisters.of(IDENTITY_START),
        )
        assertEquals(
            Cve43499PerfPass.Missing("perf-no-candidates"),
            parse(ring, head = 280L, tail = 0L),
        )
    }

    @Test
    fun `tie takes the address seen first`() {
        val ring = TestRing(DATA_SIZE)
        ring.writeSample(
            tail = 0L,
            abi = ABI_64,
            registers = TieRegisters.of(TASK, TASK + 0x200L),
        )
        ring.writeSample(
            tail = 280L,
            abi = ABI_64,
            registers = TieRegisters.of(TASK + 0x200L, TASK),
        )
        val result = parse(ring, head = 560L, tail = 0L) as Cve43499PerfPass.Address
        assertEquals(TASK, result.address)
        assertEquals(2L, result.votes)
        assertEquals(4L, result.candidates)
    }

    @Test
    fun `misaligned winner is evidence-only when task alignment is set`() {
        val misaligned = TASK + 8L
        val ring = TestRing(DATA_SIZE)
        ring.writeSample(tail = 0L, abi = ABI_64, registers = TaskRegisters.of(misaligned))
        val result = parse(ring, head = 280L, tail = 0L) as Cve43499PerfPass.Address
        assertEquals(misaligned, result.address)
        assertEquals(false, result.alignmentOk)
    }

    @Test
    fun `abi2 280-byte sample is accepted with 8-byte registers`() {
        /* G1 证据：A301SO 内核把 64 位进程 regs 标为 ABI=2，但 record=280B。 */
        val ring = TestRing(DATA_SIZE)
        ring.writeSample(tail = 0L, abi = ABI_32, registers = TaskRegisters.of(TASK))
        val result = parse(ring, head = 280L, tail = 0L) as Cve43499PerfPass.Address
        assertEquals(TASK, result.address)
        assertEquals(1L, result.votes)
        assertEquals(1L, result.spVotes)
    }

    @Test
    fun `abi2 with wrong size is record-invalid`() {
        for (size in listOf(272L, 279L, 281L)) {
            val ring = TestRing(DATA_SIZE)
            ring.writeSample(tail = 0L, abi = ABI_32, registers = TaskRegisters.of(TASK))
            ring.putU16(6L, size)
            assertEquals(
                Cve43499PerfPass.Missing("perf-record-invalid"),
                parse(ring, head = size, tail = 0L),
            )
        }
    }

    @Test
    fun `unknown abi value is record-invalid`() {
        val ring = TestRing(DATA_SIZE)
        ring.writeSample(tail = 0L, abi = 3L, registers = TaskRegisters.of(TASK))
        assertEquals(
            Cve43499PerfPass.Missing("perf-record-invalid"),
            parse(ring, head = 280L, tail = 0L),
        )
    }

    @Test
    fun `empty ring is no-samples`() {
        val ring = TestRing(DATA_SIZE)
        assertEquals(
            Cve43499PerfPass.Missing("perf-no-samples"),
            parse(ring, head = 0L, tail = 0L),
        )
    }

    private fun parse(ring: TestRing, head: Long, tail: Long): Cve43499PerfPass =
        Cve43499PerfSampleParser.parse(
            ring = ring.bytes,
            dataSize = DATA_SIZE,
            dataHead = head,
            dataTailBefore = tail,
            config = CONFIG,
        )

    private companion object {
        const val DATA_SIZE = 0x10000L
        const val ABI_64 = 1L
        const val ABI_32 = 2L
        const val ABI_NONE = 0L
        val TASK = 0xffffff896fe31200uL.toLong()
        val IDENTITY_START = 0xffffff8000000000uL.toLong()
        val IDENTITY_END = 0xffffff9000000000uL.toLong()

        val CONFIG = Cve43499PerfParseConfig(
            identityStart = IDENTITY_START,
            identityEnd = IDENTITY_END,
            tagNibble = 0xfL,
            tagShift = 56L,
            registerCount = 32,
            spRegisterIndex = 31,
            candidateCapacity = 256,
            sampleType = Cve43499PerfSampleFormat.SAMPLE_IP_BIT or
                Cve43499PerfSampleFormat.SAMPLE_REGS_INTR_BIT,
            taskAlignment = 0x200L,
        )
    }
}

/** 逐字节合成 perf ring 的测试工具（跨环写按 `% dataSize` 组装）。 */
internal class TestRing(val dataSize: Long) {
    val bytes: ByteArray = ByteArray(dataSize.toInt())

    /** 写一个 ABI=64 的完整 sample（280B）。 */
    fun writeSample(tail: Long, abi: Long, registers: LongArray) {
        val hasRegisters = abi == 1L || abi == 2L
        val size = if (hasRegisters) 280L else 24L
        val ip = 0xffffff8012345678uL.toLong()
        putU32(tail, 9L)
        putU16(tail + 4L, 0L)
        putU16(tail + 6L, size)
        putU64(tail + 8L, ip)
        putU64(tail + 16L, abi)
        if (hasRegisters) {
            for (index in registers.indices) {
                putU64(tail + 24L + index.toLong() * 8L, registers[index])
            }
        }
    }

    /** 写一个 ABI=0 的 24B record（无寄存器载荷）。 */
    fun writeAbiNoneRecord(tail: Long) {
        putU32(tail, 9L)
        putU16(tail + 6L, 24L)
        putU64(tail + 8L, 0xffffff8012345678uL.toLong())
        putU64(tail + 16L, 0L)
    }

    fun putU64(position: Long, value: Long) {
        for (index in 0 until 8) {
            putByte(position + index, ((value ushr (8 * index)) and 0xFFL).toInt())
        }
    }

    fun putU32(position: Long, value: Long) {
        for (index in 0 until 4) {
            putByte(position + index, ((value ushr (8 * index)) and 0xFFL).toInt())
        }
    }

    fun putU16(position: Long, value: Long) {
        for (index in 0 until 2) {
            putByte(position + index, ((value ushr (8 * index)) and 0xFFL).toInt())
        }
    }

    private fun putByte(position: Long, value: Int) {
        bytes[(position % dataSize).toInt()] = value.toByte()
    }
}

/** 生成 ABI=64 sample 的 32 个寄存器：只有 sp(=31) 放目标值。 */
internal object TaskRegisters {
    fun of(restored: Long): LongArray {
        val registers = LongArray(32)
        registers[31] = restored and 0xF0FFFFFFFFFFFFFFuL.toLong()
        return registers
    }
}

/** 两个候选各出现一次（测试平票取先出现）。 */
internal object TieRegisters {
    fun of(first: Long, second: Long): LongArray {
        val registers = LongArray(32)
        registers[0] = first and 0xF0FFFFFFFFFFFFFFuL.toLong()
        registers[1] = second and 0xF0FFFFFFFFFFFFFFuL.toLong()
        return registers
    }
}
