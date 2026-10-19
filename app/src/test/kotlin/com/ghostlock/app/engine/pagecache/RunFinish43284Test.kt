package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 收尾账的五条判据（本批的可执行约束）。
 *
 * 1. `run.43284 finish:` 的字段顺序：前十二个与参照 `run.chain`（ghostlock-app-ref@5f184760
 *    的 `src/core/backend/cve_2026_43284/stage_runner.cpp:697-742`）逐字同序，trigger/wait 的五个在后；
 * 2. `written` / `verified` 是 **native 的账**（noteOutcome 累加），不再按 `bytes / 16` 推算；
 *    抽样（noteSampled）只进 `verify sampling:` 行，不覆盖 finish 行；
 * 3. `rolled_back` / `journal_overflow` / `rollback_incomplete` 由 native 透传（不再恒 0）；
 * 4. 两段写要累加（钩子 2 块 + 镜像 4 块 ⇒ written=6），verified 与 native 两次回报之和一致；
 * 5. 失败路径（异常/早退）也要打这两行，并且 `error=` / `hook_error=` 带具名原因。
 */
class RunFinish43284Test {

    /** 前十二个字段与 run.chain 逐字同序；后五个是 run.trigger / run.wait。 */
    private val referenceOrder = listOf(
        "written", "verified", "rolled_back", "cleanup", "journal_overflow",
        "rollback_incomplete", "crash_dump", "hook_planned", "hook_armed", "hook",
        "hook_restored", "hook_error",
        "fired", "wait_incomplete", "wait", "terminus", "error",
    )

    @Test
    fun theFinishLineKeepsTheReferenceFieldOrder() {
        val line = RunFinish43284().finishLine()
        assertTrue("行首必须逐字是 run.43284 finish: ", line.startsWith(RunFinish43284.PREFIX))

        val tokens = line.removePrefix(RunFinish43284.PREFIX).split(" ")
        val keys = tokens.map { token -> token.substringBefore('=') }
        assertEquals("字段顺序固定（run.chain 十二个在前，trigger/wait 五个在后）", referenceOrder, keys)

        /* 前十二个字段的默认值：与参照 run.chain 行同形（未发生的事实一律 0 / None）。 */
        assertEquals(
            "written=0 verified=0 rolled_back=0 cleanup=0 journal_overflow=0 rollback_incomplete=0" +
                " crash_dump=0 hook_planned=0 hook_armed=0 hook=0 hook_restored=0 hook_error=None",
            line.removePrefix(RunFinish43284.PREFIX).substringBefore(" fired="),
        )

        /* 布尔一律 0/1（ref 的 append_bool 同形）：不能让任何一项退化成 true/false 或空串。 */
        val values = tokens.associate { token -> token.substringBefore('=') to token.substringAfter('=') }
        for (key in listOf(
            "cleanup", "journal_overflow", "rollback_incomplete", "crash_dump", "hook_planned",
            "hook_armed", "hook", "hook_restored", "fired", "wait_incomplete", "terminus",
        )) {
            val value = values.getValue(key)
            assertTrue(key + " 必须是 0/1，实际 " + value, value == "0" || value == "1")
        }
    }

    @Test
    fun verifiedCountsOnlyBlocksThatWereReadBack() {
        val finish = RunFinish43284()
        finish.noteModuleWritten(12304)
        assertEquals("模块区按 16 B 一块：12304 / 16 = 769", 769, finish.moduleBlocks)
        assertEquals("noteModuleWritten 不再推算 written（写成几块只有 native 的账知道）", 0, finish.written)
        assertEquals("写成功不等于验过：还没回读", 0, finish.verified)

        val image = ByteArray(12304) { index -> (index % 251).toByte() }
        val indices = VerifySampling43284.indices(769)
        assertEquals("512 B 粒度 = 首块 + 每 32 块 + 末块", 25, indices.size)
        assertEquals("首块必读", 0, indices.first())
        assertEquals("末块必读", 768, indices.last())

        /* 25 个抽样块里只有前 5 块读得回来：第 5 个抽样块（序号 128）内容不符，其余 20 块读失败。 */
        val sample = VerifySampling43284.sample(
            totalBlocks = 769,
            expected = { index -> VerifySampling43284.blockAt(image, index) },
            read = { index ->
                when (indices.indexOf(index)) {
                    0, 1, 2, 3 -> VerifySampling43284.blockAt(image, index)
                    4 -> ByteArray(16) { 0x7F }
                    else -> null
                }
            },
        )
        assertEquals("规则选中 25 块", 25, sample.planned)
        assertEquals("真的读回来 5 块", 5, sample.returned)
        assertEquals("比中 4 块", 4, sample.verified)
        assertEquals("不符的是序号 128 那一块", listOf(128), sample.mismatched)
        assertEquals("读不回来的 20 块不进账", 20, sample.unreadable.size)

        finish.noteSampled(sample.verified)
        assertEquals("抽样行只数真正回读过的块", 4, finish.samplingVerified)
        assertEquals("抽样不覆盖 native 的 verified（没有账就是 0）", 0, finish.verified)
        assertEquals("verify sampling: every=32 verified=4 of 769", finish.samplingLine())

        /* native 报账之后，finish 行的 verified 以它为准（与抽样行的 4 是两回事）。 */
        finish.noteOutcome(written = 769, verified = 25, rolled = 0, overflow = 0, incomplete = 0)
        assertTrue("finish 行里的 verified 来自 noteOutcome", finish.finishLine().contains(" verified=25 "))
        assertEquals("抽样行保持自己的数（不跟 finish 行一起变）", 4, finish.samplingVerified)
    }

    /**
     * ① 两段写累加：钩子 2 块 + 镜像 4 块 ⇒ written=6；verified 与 native 两次回报之和一致。
     */
    @Test
    fun twoWritesAddUpToTheNativeTotals() {
        val finish = RunFinish43284()

        /* 第一段：钩子载荷 2 块（native 回报 written=2 verified=2）。 */
        finish.noteOutcome(written = 2, verified = 2, rolled = 0, overflow = 0, incomplete = 0)
        /* 第二段：模块镜像 4 块（native 回报 written=4 verified=3 —— 有一块回读不符）。 */
        finish.noteOutcome(written = 4, verified = 3, rolled = 0, overflow = 0, incomplete = 0)

        assertEquals("两段写累加：2 + 4 = 6", 6, finish.written)
        assertEquals("verified = native 两次回报之和（2 + 3），不是抽样数、不是 bytes/16", 5, finish.verified)
        assertTrue("finish 行带累计值", finish.finishLine().contains(" written=6 verified=5 "))

        /* 抽样数与 native 的账互不覆盖。 */
        finish.noteSampled(1)
        assertEquals("抽样数只进 sampling 行", 1, finish.samplingVerified)
        assertEquals("抽样不改变 finish 行的 verified", 5, finish.verified)
    }

    /**
     * ② 回滚三项透传：注入非零值之后 finish 行不再是 0。
     */
    @Test
    fun rollbackNumbersPassThroughInsteadOfStayingZero() {
        val finish = RunFinish43284()
        finish.noteOutcome(written = 4, verified = 1, rolled = 3, overflow = 1, incomplete = 1)

        assertEquals("回滚块数原样记账", 3, finish.rolledBack)
        assertTrue("journal 溢出置位", finish.journalOverflow)
        assertTrue("回滚不完整置位", finish.rollbackIncomplete)
        val line = finish.finishLine()
        assertTrue("finish 行带 rolled_back=3", line.contains(" rolled_back=3 "))
        assertTrue("finish 行带 journal_overflow=1", line.contains(" journal_overflow=1 "))
        assertTrue("finish 行带 rollback_incomplete=1", line.contains(" rollback_incomplete=1 "))

        /* 第二次回报：rolled 继续累加；两个标志是「任一次为真」而不是计数。 */
        finish.noteOutcome(written = 2, verified = 2, rolled = 1, overflow = 0, incomplete = 0)
        assertEquals("回滚块数按调用累加", 4, finish.rolledBack)
        assertEquals("written 继续累加", 6, finish.written)
        assertTrue("标志保持置位", finish.journalOverflow)
    }

    /**
     * ② 的数组形：probe 给 native 的那个 `LongArray(5)` 直接进账；空/短数组不猜数。
     */
    @Test
    fun theNativeOutcomeArrayIsWhatGetsRecorded() {
        val finish = RunFinish43284()
        finish.noteOutcome(longArrayOf(2, 2, 0, 0, 0))
        finish.noteOutcome(longArrayOf(4, 3, 1, 1, 0))
        assertEquals(6, finish.written)
        assertEquals(5, finish.verified)
        assertEquals(1, finish.rolledBack)
        assertTrue(finish.journalOverflow)

        finish.noteOutcome(LongArray(4))
        finish.noteOutcome(null)
        assertEquals("短数组/空数组不是「全 0 的一次写」，而是没拿到账", 6, finish.written)
        assertEquals(5, finish.verified)
    }

    @Test
    fun aFailedRunStillEmitsTheFinishLineWithItsErrorNames() {
        val finish = RunFinish43284()
        val lines = mutableListOf<String>()
        /* 与 probe43284 的 finally 同形：先抛，再看收尾落了几行、带的是哪个名字。 */
        try {
            finish.noteError("module-write:image-write-refused")
            finish.noteError("plan:late-error-must-not-overwrite")
            finish.noteHookError("hook-restore-refused")
            finish.noteTrigger(false)
            throw IllegalStateException("boom")
        } catch (boom: IllegalStateException) {
            assertEquals("异常本身不被收尾吞掉", "boom", boom.message)
        } finally {
            finish.emit { line -> lines += line }
        }

        assertEquals("一次运行各一行：抽样率 + finish", 2, lines.size)
        assertEquals("verify sampling: every=32 verified=0 of 0", lines[0])

        val line = lines[1]
        assertTrue("行首逐字", line.startsWith(RunFinish43284.PREFIX))
        assertTrue("第一个 error 名在行尾", line.endsWith(" error=module-write:image-write-refused"))
        assertTrue("第一个 hook_error 名在行内", line.contains(" hook_error=hook-restore-refused"))
        assertTrue("恢复没成功 ⇒ hook_restored=0", line.contains(" hook_restored=0"))
        assertTrue("触发失败 ⇒ fired=0", line.contains(" fired=0"))
        assertFalse("后到的 error 不覆盖第一个", line.contains("plan:late-error-must-not-overwrite"))
    }
}
