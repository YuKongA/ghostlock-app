package com.ghostlock.app.tempui

import com.ghostlock.app.jni.cve43284.Assembled43284

/**
 * 回读抽样的唯一规则（Kotlin 侧；与 native 写路径的 `request.verify_every` 同一条）。
 *
 * native 的规则（`core/src/backend/cve_2026_43284/steps/pagecache_steps.hpp:60-74`）：
 * **首块、每 N 块、末块**回读；N 默认 32（32 × 16 B = 512 B，与「每 512 字节一行」的日志门同一粒度）。
 * 抽样只减少**读**的次数，不放宽判据：任何一次回读不符仍然整段失败（fail-closed）。
 *
 * 本对象做的是**同一件事的 Kotlin 侧记账**：`run.43284 finish:` 里的 `verified=` 只数
 * 「真的读回来且逐字节比中」的块 —— 没读的块不算，读失败的不算，读回来不符的更不算。
 *
 * @note 抽样不改变写路径：它只在写完之后按同一规则补一次回读，用来给出**真实的** verified。
 */
object VerifySampling43284 {

    /** 抽样间隔：32 块 × 16 B = 512 B（与「每 512 字节一行」的日志门同粒度）。 */
    const val EVERY_BLOCKS: Int = 32

    /**
     * 一次抽样的账。
     *
     * @param planned    抽样规则选中的块数（[indices] 的大小）
     * @param returned   真的拿到字节的块数（读失败不计）
     * @param verified   拿到字节且逐字节比中的块数 —— **只有它**进 `run.43284 finish:` 的 verified
     * @param mismatched 读回来了但内容不符的块序号
     * @param unreadable 读不回来的块序号
     */
    class Sample(
        val planned: Int,
        val returned: Int,
        val verified: Int,
        val mismatched: List<Int>,
        val unreadable: List<Int>,
    )

    /**
     * 抽样选块：首块、每 [every] 块、末块（与 native 的 verify_every 同一条规则）。
     *
     * @param totalBlocks 区域总块数（≤0 ⇒ 空表）
     * @param every       间隔；≤0 按 1 处理（= 每块都读，与 native 的 0 同义）
     * @return 升序、无重复的块序号
     */
    fun indices(totalBlocks: Int, every: Int = EVERY_BLOCKS): List<Int> {
        if (totalBlocks <= 0) {
            return emptyList()
        }
        val step = if (every <= 0) 1 else every
        val picked = sortedSetOf<Int>()
        var at = 0
        while (at < totalBlocks) {
            picked += at
            at += step
        }
        picked += totalBlocks - 1
        return picked.toList()
    }

    /**
     * 按抽样规则回读一遍，只把**真的读回来且比中**的块记进 verified。
     *
     * @param totalBlocks 区域总块数
     * @param expected    期望字节（按块序号；null ⇒ 该块没有期望值，计为不符）
     * @param read        回读（按块序号；null ⇒ 读失败）
     * @param every       间隔（见 [indices]）
     * @return 这次的账
     */
    fun sample(
        totalBlocks: Int,
        expected: (Int) -> ByteArray?,
        read: (Int) -> ByteArray?,
        every: Int = EVERY_BLOCKS,
    ): Sample {
        val selected = indices(totalBlocks, every)
        val mismatched = mutableListOf<Int>()
        val unreadable = mutableListOf<Int>()
        var returned = 0
        var verified = 0
        for (index in selected) {
            val got = read(index)
            if (got == null) {
                unreadable += index
                continue
            }
            returned += 1
            val want = expected(index)
            if (want != null && got.contentEquals(want)) {
                verified += 1
            } else {
                mismatched += index
            }
        }
        return Sample(selected.size, returned, verified, mismatched, unreadable)
    }

    /**
     * 取镜像里的第 [index] 块（末块按零补齐到一块宽，与写路径同）。
     *
     * @param image 整段镜像
     * @param index 块序号（0 起）
     * @return 一块；序号越界 ⇒ null
     */
    fun blockAt(image: ByteArray, index: Int): ByteArray? {
        val at = index * Assembled43284.BLOCK
        if (at < 0 || at >= image.size) {
            return null
        }
        val end = minOf(at + Assembled43284.BLOCK, image.size)
        return image.copyOfRange(at, end).copyOf(Assembled43284.BLOCK)
    }
}
