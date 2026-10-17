package com.ghostlock.app.tempui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 收尾账的三条判据（本批新增的唯一可执行约束）。
 *
 * 1. `run.43284 finish:` 的字段顺序：前十二个与参照 `run.chain`（ghostlock-app-ref@5f184760
 *    的 `src/core/backend/cve_2026_43284/stage_runner.cpp:697-742`）逐字同序，trigger/wait 的五个在后；
 * 2. `verified` 只数**真的读回来且比中**的块（抽样模式下没读的块不算，读失败/不符也不算）；
 * 3. 失败路径（异常/早退）也要打这两行，并且 `error=` / `hook_error=` 带具名原因。
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
        assertEquals("模块区按 16 B 一块：12304 / 16 = 769", 769, finish.written)
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
        assertEquals("verified 只数真正回读过的块", 4, finish.verified)
        assertEquals("verify sampling: every=32 verified=4 of 769", finish.samplingLine())
        assertTrue("finish 行里的 verified 与抽样行同源", finish.finishLine().contains(" verified=4 "))
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
