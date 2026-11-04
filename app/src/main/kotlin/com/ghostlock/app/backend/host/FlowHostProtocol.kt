package com.ghostlock.app.backend.host

import com.ghostlock.app.core.flow.FlowResult
import com.ghostlock.app.backend.launcher.BackendExecutionState

/**
 * `:flowhost` 通道的稳定码、状态与终态 verify 行协议（B2a）。
 *
 * 用途：host 与 launcher 只通过本文件的常量/编解码交换状态，避免跨进程字符串漂移。
 * verify 行协议（UTF-8，key=value，固定顺序、每行一条、末尾换行）：
 * `state=terminal`、`result=ok|failed|handoff`、`reason=<escaped>`（仅 failed）、
 * `steps=<n>`、`evidence.<key>=<escaped>`（可 0..n 行）。
 *
 * @note 纯 JVM：不依赖 Android/Bundle；Bundle 编解码在 host/launcher 层。
 * @note 阶段：B2a（新进程攻击机制）；设备门禁 B2b。
 */
object FlowHostProtocol {
    /** 成功。 */
    const val OK: Int = 0

    /** 参数/预算非法。 */
    const val ERR_ARGS: Int = 1

    /** symbol 非法/未登记/校验失败。 */
    const val ERR_SYMBOL: Int = 2

    /** 已有 run/准备中，不可重复。 */
    const val ERR_BUSY: Int = 3

    /** 状态机不允许该操作（如未 prepare 就 start）。 */
    const val ERR_STATE: Int = 4

    /** 后台执行线程启动失败。 */
    const val ERR_START: Int = 5

    /** 日志/verify 写回失败（IO 层）。 */
    const val ERR_IO: Int = 6

    /** 尚未 prepare。 */
    const val IDLE: Int = 0

    /** 已 prepare，可 start。 */
    const val PREPARED: Int = 1

    /** 正在后台执行 Flow。 */
    const val RUNNING: Int = 2

    /** 已产生终态（verify 可读）。 */
    const val TERMINAL: Int = 3

    /** payload 上限（计划 §2.3：≤512 KiB）。 */
    const val MAX_PAYLOAD_BYTES: Int = 512 * 1024

    /** onTerminal 终止码：ok。 */
    const val TERMINAL_CODE_OK: Int = 0

    /** onTerminal 终止码：failed。 */
    const val TERMINAL_CODE_FAILED: Int = 1

    /** onTerminal 终止码：handoff（不报成功）。 */
    const val TERMINAL_CODE_HANDOFF: Int = 2

    /** readLog 单次上限（64 KiB；防 Binder 事务超限）。 */
    const val MAX_LOG_BYTES: Int = 64 * 1024

    /** verify 固定首行。 */
    const val VERIFY_STATE_TERMINAL: String = "terminal"

    /** 终态结果：成功。 */
    const val RESULT_OK: String = "ok"

    /** 终态结果：失败。 */
    const val RESULT_FAILED: String = "failed"

    /** 终态结果：已请求 handoff（不报成功）。 */
    const val RESULT_HANDOFF: String = "handoff"

    /** Flow evidence 中声明 handoff 的键。 */
    const val HANDOFF_EVIDENCE_KEY: String = "handoff"

    /** Flow evidence 中 handoff 的取值。 */
    const val HANDOFF_EVIDENCE_VALUE: String = "requested"

    /** 终态编码/解码后的中性结果。 */
    enum class TerminalResult { OK, FAILED, HANDOFF }

    /**
     * 解码后的 verify 终态。
     *
     * @property result 中性终态（OK/FAILED/HANDOFF）。
     * @property reason 仅 FAILED 非空；稳定可检索。
     * @property steps Flow 报告的 step 数（≥0）。
     * @property evidence Flow evidence（已反转义）。
     */
    data class VerifiedTerminal(
        val result: TerminalResult,
        val reason: String?,
        val steps: Int,
        val evidence: Map<String, String>,
    )

    /**
     * 编码 verify 行协议。
     *
     * @param result host 收到的 FlowResult。
     * @param handoff true 表示本次结果被映射为 handoff（仅允许 Ok/无失败结果）。
     * @return UTF-8 字节（末尾换行）；可直接跨 Binder 返回。
     * @throws IllegalArgumentException handoff 与非 Ok 结果同时出现，或 evidence key 非法。
     */
    fun encodeVerify(result: FlowResult, handoff: Boolean = false): ByteArray {
        if (handoff && result !is FlowResult.Ok) {
            throw IllegalArgumentException("flow-host:verify:handoff-requires-ok")
        }
        val lines = ArrayList<String>()
        lines += "state=" + VERIFY_STATE_TERMINAL
        val resultLine = when {
            handoff -> RESULT_HANDOFF
            result is FlowResult.Ok -> RESULT_OK
            else -> RESULT_FAILED
        }
        lines += "result=" + resultLine
        if (resultLine == RESULT_FAILED) {
            lines += "reason=" + escape((result as FlowResult.Failed).reason)
        }
        lines += "steps=" + result.steps.size
        for ((key, value) in result.evidence) {
            require(key.isNotEmpty() && '=' !in key && '\n' !in key && '\r' !in key) {
                "flow-host:verify:bad-evidence-key:$key"
            }
            lines += "evidence." + escape(key) + "=" + escape(value)
        }
        return (lines.joinToString("\n") + "\n").toByteArray(Charsets.UTF_8)
    }

    /**
     * 解码 verify 行协议。
     *
     * @param bytes host 返回的字节；空数组表示未终态（由调用方按状态机处理）。
     * @return 校验通过的 [VerifiedTerminal]。
     * @throws IllegalArgumentException 具名 `flow-host:verify:*`（缺行/顺序/值域/转义无效）。
     */
    fun decodeVerify(bytes: ByteArray): VerifiedTerminal {
        if (bytes.isEmpty()) throw IllegalArgumentException("flow-host:verify:empty")
        val text = bytes.toString(Charsets.UTF_8)
        if (!text.endsWith("\n")) {
            throw IllegalArgumentException("flow-host:verify:missing-final-newline")
        }
        val lines = text.removeSuffix("\n").split('\n')
        if (lines.isEmpty()) throw IllegalArgumentException("flow-host:verify:empty")
        val entries = ArrayList<Pair<String, String>>(lines.size)
        for (line in lines) {
            val split = splitFirstUnescaped(line)
                ?: throw IllegalArgumentException("flow-host:verify:malformed-line:" + line.take(64))
            entries += split
        }
        val seen = HashSet<String>()
        var state: String? = null
        var result: String? = null
        var reason: String? = null
        var steps: Int? = null
        val evidence = LinkedHashMap<String, String>()
        for ((rawKey, rawValue) in entries) {
            val key = unescape(rawKey)
            val value = unescape(rawValue)
            if (!seen.add(key)) {
                throw IllegalArgumentException("flow-host:verify:duplicate-key:$key")
            }
            when {
                key == "state" -> state = value
                key == "result" -> result = value
                key == "reason" -> reason = value
                key == "steps" -> {
                    steps = value.toIntOrNull()
                        ?: throw IllegalArgumentException("flow-host:verify:bad-steps:" + value)
                    if (steps < 0) {
                        throw IllegalArgumentException("flow-host:verify:bad-steps:" + value)
                    }
                }
                key.startsWith("evidence.") && key.length > "evidence.".length ->
                    evidence[key.removePrefix("evidence.")] = value
                else -> throw IllegalArgumentException("flow-host:verify:unknown-key:$key")
            }
        }
        if (state != VERIFY_STATE_TERMINAL) {
            throw IllegalArgumentException("flow-host:verify:bad-state:" + state)
        }
        val resultKind = when (result) {
            RESULT_OK -> TerminalResult.OK
            RESULT_FAILED -> TerminalResult.FAILED
            RESULT_HANDOFF -> TerminalResult.HANDOFF
            else -> throw IllegalArgumentException("flow-host:verify:bad-result:" + result)
        }
        val parsedSteps = steps
            ?: throw IllegalArgumentException("flow-host:verify:missing-steps")
        if (resultKind == TerminalResult.FAILED && (reason.isNullOrBlank())) {
            throw IllegalArgumentException("flow-host:verify:missing-reason")
        }
        return VerifiedTerminal(
            result = resultKind,
            reason = if (resultKind == TerminalResult.FAILED) reason else null,
            steps = parsedSteps,
            evidence = evidence,
        )
    }

    /**
     * 把 verify 终态映射为 UI 中性状态。
     *
     * @param verified 已解码终态。
     * @param profileIdentity 可选持久化身份；非 null 时并入 evidence（`profileIdentity`）。
     * @return OK→Finished(Ok)、FAILED→Finished(Failed)、HANDOFF→[BackendExecutionState.HandoffRequested]。
     * @note HANDOFF 不报成功/不等待/不写状态（计划 §2.5）。
     */
    fun mapTerminal(
        verified: VerifiedTerminal,
        profileIdentity: String? = null,
    ): BackendExecutionState {
        val evidence = if (profileIdentity.isNullOrBlank()) {
            verified.evidence
        } else {
            verified.evidence + ("profileIdentity" to profileIdentity)
        }
        return when (verified.result) {
            TerminalResult.OK -> BackendExecutionState.Finished(
                FlowResult.Ok(steps = emptyList(), evidence = evidence),
            )
            TerminalResult.FAILED -> BackendExecutionState.Finished(
                FlowResult.Failed(
                    reason = verified.reason ?: "flow-host:verify:missing-reason",
                    failedStep = null,
                    steps = emptyList(),
                    evidence = evidence,
                ),
            )
            TerminalResult.HANDOFF -> BackendExecutionState.HandoffRequested
        }
    }

    /** @return 转义后的文本（`\\`/`\n`/`\r`）。 */
    private fun escape(value: String): String = buildString(value.length) {
        for (char in value) {
            when (char) {
                '\\' -> append("\\\\")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                else -> append(char)
            }
        }
    }

    /** @return 反转义文本；非法转义序列按原样保留（协议层不静默丢字节）。 */
    private fun unescape(value: String): String = buildString(value.length) {
        var index = 0
        while (index < value.length) {
            val char = value[index]
            if (char == '\\' && index + 1 < value.length) {
                when (val next = value[index + 1]) {
                    '\\' -> { append('\\'); index += 2 }
                    'n' -> { append('\n'); index += 2 }
                    'r' -> { append('\r'); index += 2 }
                    else -> { append(char); index += 1 }
                }
            } else {
                append(char)
                index += 1
            }
        }
    }

    /** @return 键/值拆分（仅第一个未转义 `=`）；无 `=` 返回 null。 */
    private fun splitFirstUnescaped(line: String): Pair<String, String>? {
        var escaped = false
        for (index in line.indices) {
            val char = line[index]
            if (escaped) { escaped = false; continue }
            if (char == '\\') { escaped = true; continue }
            if (char == '=') return line.substring(0, index) to line.substring(index + 1)
        }
        return null
    }
}
