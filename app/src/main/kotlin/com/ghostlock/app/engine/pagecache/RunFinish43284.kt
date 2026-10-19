package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.jni.cve43284.Assembled43284

/**
 * 一次 43284 运行的收尾账：`run.43284 finish:` 行与 `verify sampling:` 行的**唯一**来源。
 *
 * # 为什么要有这个类
 * 参照实现 ghostlock-app-ref@5f184760 把收尾写成三段结构化行
 * （`src/core/backend/cve_2026_43284/stage_runner.cpp:697-742`）：
 * `run.chain`（written / verified / rolled_back / cleanup / journal_overflow / rollback_incomplete /
 * crash_dump / hook_planned / hook_armed / hook / hook_restored / hook_error）、
 * `run.trigger`（fired / wait_incomplete）与 `run.wait`（outcome / terminus / error）。
 * 本仓的链在 Kotlin 侧（tempui），此前没有这三行的对应物 —— 真机日志里看不出「这次写了几块、
 * 验了几块、钩子收没收回来」。本类把这些事实聚在一处，并在一次运行的收尾**各打一行**。
 *
 * # 字段与顺序
 * 前十二个字段与 ref 的 `run.chain` **逐字同序**；后五个字段是 ref 的 `run.trigger` / `run.wait`
 * 两行（同一批事实并进同一行，免得一次运行多出两条独立事件）。**顺序是判据**，由
 * `RunFinish43284Test.theFinishLineKeepsTheReferenceFieldOrder` 钉住。
 *
 * # 值只来自本次运行统计到的变量（不猜数）
 * | 字段 | 来源 | 含义 |
 * |---|---|---|
 * | written | [noteOutcome] | native 每次写镜像回报的 written 之和（**不再**按 `bytes / 16` 推算） |
 * | verified | [noteOutcome] | native 回报的 verified 之和（**不是**抽样数；抽样数只进 `verify sampling:` 行） |
 * | rolled_back | [noteOutcome] | native 回报的回滚块数之和（不再恒 0） |
 * | cleanup | [noteCleanup] | 终点收尾（拆 Access / 拆落地 / 拆分步会话 / 关会话）是否全部报成功 |
 * | journal_overflow | [noteOutcome] | native 回报的 journal 溢出（任一次为真即真；不再恒 0） |
 * | rollback_incomplete | [noteOutcome] | native 回报的回滚不完整（任一次为真即真；不再恒 0） |
 * | crash_dump | [noteCrashDump] | patch#1（crash_dump64 的 helper）整段写成功 |
 * | hook_planned | [noteHookPlanned] | hookPlan 拿到了（ref：资产准备好了，不是「挂上了」） |
 * | hook_armed | [noteHookArmed] | 钩子载荷真的落进 libc++ 页 |
 * | hook | [noteHookApplied] | 站点字写成功（chain 的 applied 标志） |
 * | hook_restored | [noteHookRestored] | 终点恢复把站点写回并回读一致 |
 * | hook_error | [noteHookError] | 钩子侧**第一个**具名原因（ref 的 hook_error 语义） |
 * | fired | [noteTrigger] | 触发调用成功 |
 * | wait_incomplete | [noteWait] | 预算内一直是 inflight |
 * | wait | [noteWait] | 终态名（`WaitPolicy43284.terminalName`；没走到 wait 段 ⇒ `none`） |
 * | terminus | [noteWait] | 成功终态 |
 * | error | [noteError] | 本次运行**第一个**具名失败 |
 *
 * @param every 抽样间隔（块）。默认 32 块 × 16 B = 512 B，与「每 512 字节一行」的日志门同粒度。
 */
class RunFinish43284(private val every: Int = VerifySampling43284.EVERY_BLOCKS) {

    /**
     * 本次运行**真实**写成的块数：native 每次 `writeImage` 回报的 written 之和（见 [noteOutcome]）。
     *
     * @note 不再由 `bytes / 16` 推算，也不再是常量：只有报了账的写才计数。
     */
    var written: Int = 0
        private set

    /**
     * 本次运行**真实**验成的块数：native 每次回报的 verified 之和。
     *
     * @note [noteSampled] 的抽样结果不进这里（它只服务 `verify sampling:` 行）：写路径的账以 native 为准。
     */
    var verified: Int = 0
        private set

    /** 抽样回读（[VerifySampling43284]）数出来的块数：只进 `verify sampling:` 行。 */
    var samplingVerified: Int = 0
        private set

    /** 模块区的总块数（抽样率行的分母）。 */
    var moduleBlocks: Int = 0
        private set

    private var crashDump: Boolean = false
    private var hookPlanned: Boolean = false
    private var hookArmed: Boolean = false
    private var hookApplied: Boolean = false
    private var hookRestored: Boolean = false
    private var hookError: String = NONE
    private var fired: Boolean = false
    private var waitIncomplete: Boolean = false
    private var waitName: String = WAIT_NONE
    private var terminus: Boolean = false
    private var cleanup: Boolean = false
    private var error: String = NONE

    /**
     * native 回报的回滚块数（[noteOutcome] 按调用累加）。
     *
     * @note 这是**真的**回滚条数，不是常量 0：写失败且 journal 写回成功时它才非零；
     *       native 的 `write outcome: … rolled=…` 行与它是同一份账。
     */
    var rolledBack: Int = 0
        private set

    /** native 回报的 journal 容量不足（任一次为真即真；溢出只记录、不中止写，与参照实现同）。 */
    var journalOverflow: Boolean = false
        private set

    /** native 回报的回滚不完整（有块写不回/回读不符；任一次为真即真）。 */
    var rollbackIncomplete: Boolean = false
        private set

    /**
     * 记一次模块区的整段写结果：只定**总块数**（抽样率行的分母）。
     *
     * @param bytes 镜像字节数；≤0 ⇒ 0 块
     * @note 这里**不再**推算 written：写成几块只有 native 的账知道（[noteOutcome]）。
     */
    fun noteModuleWritten(bytes: Int) {
        moduleBlocks = if (bytes <= 0) 0 else (bytes + Assembled43284.BLOCK - 1) / Assembled43284.BLOCK
    }

    /**
     * 记一次抽样回读的**真实**结果（只进 `verify sampling:` 行）。
     *
     * @param verifiedBlocks 真的读回来且比中的块数（[VerifySampling43284.sample] 的返回值）
     * @note finish 行的 verified 以 native 的账为准（[noteOutcome]）：抽样是补读，不是写路径的账。
     */
    fun noteSampled(verifiedBlocks: Int) {
        samplingVerified = if (verifiedBlocks <= 0) 0 else verifiedBlocks
    }

    /**
     * 记一次 native 写镜像的**真实五数**（与 `write outcome:` 行同源），按调用累加。
     *
     * @param written    这次写报成功的块数
     * @param verified   这次真回读且比中的块数
     * @param rolled     这次回滚写回的旧块条数
     * @param overflow   journal 容量不足（0/1；非零即置位，不累加）
     * @param incomplete 回滚不完整（0/1；非零即置位，不累加）
     * @note 负值按 0 处理：native 不会给负数，给了也不许把已经记账的数做小。
     */
    fun noteOutcome(written: Int, verified: Int, rolled: Int, overflow: Int, incomplete: Int) {
        this.written += atLeastZero(written)
        this.verified += atLeastZero(verified)
        rolledBack += atLeastZero(rolled)
        if (overflow != 0) {
            journalOverflow = true
        }
        if (incomplete != 0) {
            rollbackIncomplete = true
        }
    }

    /**
     * 记一次 native 写镜像的五数（数组形：native 的 outcomeOut）。
     *
     * @param outcome `[written, verified, rolled, overflow, incomplete]`；null 或短于 [OUTCOME_WORDS] ⇒ 不记
     * @note 没有账就不猜：短数组不是「全 0 的一次写」，而是一次**没拿到账**的调用。
     */
    fun noteOutcome(outcome: LongArray?) {
        if (outcome == null || outcome.size < OUTCOME_WORDS) {
            return
        }
        noteOutcome(
            written = outcome[0].toInt(),
            verified = outcome[1].toInt(),
            rolled = outcome[2].toInt(),
            overflow = outcome[3].toInt(),
            incomplete = outcome[4].toInt(),
        )
    }

    /** patch#1（crash_dump64 的 helper）整段写成功 ⇒ `crash_dump=1`。 */
    fun noteCrashDump(written: Boolean) {
        crashDump = written
    }

    /** 钩子计划是否拿到（ref 的 `hook_planned`）。 */
    fun noteHookPlanned(planned: Boolean) {
        hookPlanned = planned
    }

    /**
     * 钩子载荷是否真的落进 libc++ 页（ref 的 `hook_armed`）。
     *
     * @param armed   载荷整段写成功的判据
     * @param refusal 失败时的具名原因（空 ⇒ 用 `payload-write-refused`）
     */
    fun noteHookArmed(armed: Boolean, refusal: String = "") {
        hookArmed = armed
        if (!armed) {
            noteHookError(refusal.ifBlank { "payload-write-refused" })
        }
    }

    /**
     * 站点字是否写成功（ref 的 `hook` = chain 的 applied 标志）。
     *
     * @param applied 写钩子的返回
     * @param refusal 失败时的具名原因（空 ⇒ 用 `hook-write-refused`）
     */
    fun noteHookApplied(applied: Boolean, refusal: String = "") {
        hookApplied = applied
        if (!applied) {
            noteHookError(refusal.ifBlank { "hook-write-refused" })
        }
    }

    /**
     * 终点恢复是否把站点写回并回读一致（ref 的 `hook_restored`）。
     *
     * @param restored 恢复写的返回
     * @param refusal  失败时的具名原因（空 ⇒ 用 `hook-restore-refused`）
     */
    fun noteHookRestored(restored: Boolean, refusal: String = "") {
        hookRestored = restored
        if (!restored) {
            noteHookError(refusal.ifBlank { "hook-restore-refused" })
        }
    }

    /** 触发是否成功（ref 的 `run.trigger fired=`）。 */
    fun noteTrigger(fired: Boolean) {
        this.fired = fired
    }

    /**
     * 等终态的结论（ref 的 `run.wait outcome= terminus= error=`）。
     *
     * @param name       终态名（`WaitPolicy43284.terminalName` 的返回值）
     * @param terminus   成功终态 ⇒ `terminus=1`
     * @param incomplete 预算内一直是 inflight ⇒ `wait_incomplete=1`
     */
    fun noteWait(name: String, terminus: Boolean, incomplete: Boolean) {
        waitName = named(name)
        this.terminus = terminus
        waitIncomplete = incomplete
    }

    /**
     * 终点收尾是否整段成功（拆 Access → 拆落地 → 拆分步会话 → 关会话）。
     *
     * @param ok 全部报成功 ⇒ `cleanup=1`
     * @note 比 ref 的 `cleanup_ran`（release 跑过就算）更严：这里要求每一步都返回成功，
     *       所以「拆不干净」不会伪装成干净。
     */
    fun noteCleanup(ok: Boolean) {
        cleanup = ok
    }

    /**
     * 记本次运行的第一个具名失败（后到的不会覆盖它）。
     *
     * @param raw 具名原因（空白会被压成 '_'，保证仍在同一行内）
     */
    fun noteError(raw: String) {
        if (error == NONE) {
            error = named(raw)
        }
    }

    /**
     * 记钩子侧的第一个具名原因（ref 的 hook_error 只报**第一个**原因）。
     *
     * @param raw 具名原因（空 ⇒ 不记，调用方应给 fallback）
     */
    fun noteHookError(raw: String) {
        if (hookError == NONE) {
            hookError = named(raw)
        }
    }

    /**
     * 抽样率行：一眼看出「抽样验过的块 / 总块数」与间隔。
     *
     * @return `verify sampling: every=<n> verified=<n> of <n>`
     * @note 这个 verified 是 [noteSampled] 的（[VerifySampling43284] 补读的结果）；finish 行的 verified
     *       是 native 的账（[noteOutcome]）。两者同义不同源，所以分两处打印，不互相覆盖。
     */
    fun samplingLine(): String =
        "verify sampling: every=" + every + " verified=" + samplingVerified + " of " + moduleBlocks

    /**
     * 收尾行：ref `run.chain` 的十二个字段（逐字同序）+ `run.trigger` / `run.wait` 的五个字段。
     *
     * @return `run.43284 finish: written=… verified=… … terminus=… error=<name>`
     */
    fun finishLine(): String = buildString {
        append(PREFIX)
        append("written=").append(written)
        append(" verified=").append(verified)
        append(" rolled_back=").append(rolledBack)
        append(" cleanup=").append(flag(cleanup))
        append(" journal_overflow=").append(flag(journalOverflow))
        append(" rollback_incomplete=").append(flag(rollbackIncomplete))
        append(" crash_dump=").append(flag(crashDump))
        append(" hook_planned=").append(flag(hookPlanned))
        append(" hook_armed=").append(flag(hookArmed))
        append(" hook=").append(flag(hookApplied))
        append(" hook_restored=").append(flag(hookRestored))
        append(" hook_error=").append(hookError)
        append(" fired=").append(flag(fired))
        append(" wait_incomplete=").append(flag(waitIncomplete))
        append(" wait=").append(waitName)
        append(" terminus=").append(flag(terminus))
        append(" error=").append(error)
    }

    /**
     * 收尾两行的唯一出口（每次运行各一行，不参与抽样）。
     *
     * @param onLine 逐行回调（屏幕 / logcat / 文件同一行）
     */
    fun emit(onLine: (String) -> Unit) {
        onLine(samplingLine())
        onLine(finishLine())
    }

    /** 单行化 + 有界：名字里不容许空白（否则一行会裂成两行），长度截到 [NAME_MAX]。 */
    private fun named(raw: String): String {
        val one = raw.trim().replace(WHITESPACE, "_")
        if (one.isEmpty()) {
            return NONE
        }
        return if (one.length <= NAME_MAX) one else one.substring(0, NAME_MAX - 3) + "..."
    }

    /** 布尔一律 0/1（与 ref 的 append_bool 同形）。 */
    private fun flag(value: Boolean): String = if (value) "1" else "0"

    /** 记账只加不减：负值按 0 处理（native 不会给负数，给了也不许把账做小）。 */
    private fun atLeastZero(value: Int): Int = if (value <= 0) 0 else value

    companion object {
        /** 行首（逐字；单测按它切字段）。 */
        const val PREFIX: String = "run.43284 finish: "

        /** 没有失败时的名字（ref 的 `chain_error_name(None)` 同字）。 */
        const val NONE: String = "None"

        /** wait 段没走到时的名字（本次运行没有终态可报）。 */
        const val WAIT_NONE: String = "none"

        /** native 五数的宽度（written / verified / rolled / overflow / incomplete）。 */
        const val OUTCOME_WORDS: Int = 5

        /** 名字字段的长度上限：保证一行仍然有界。 */
        private const val NAME_MAX: Int = 64

        private val WHITESPACE = Regex("\\s+")
    }
}
