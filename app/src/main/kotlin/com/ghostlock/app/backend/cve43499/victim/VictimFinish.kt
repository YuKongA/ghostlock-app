package com.ghostlock.app.backend.cve43499.victim

import java.security.MessageDigest

/**
 * victim 内执行 finish 脚本的具名结局（R3f/task-45）。
 *
 * @note 只表达「rooted victim 内实际执行发生过什么」，不表达业务成功；验收仍由
 *   `finish/FinishAcceptance` 的具名判定完成。
 * @note 阶段：R3f（finish 交 rooted victim 执行）。
 */
internal sealed interface VictimFinishResult {
    /**
     * 脚本已在 victim 内执行完成（无论退出码是否为 0）。
     *
     * @property exitCode 脚本退出码（`/system/bin/sh <script>` 的退出码）。
     * @property outputSha256 脚本输出（stdout+stderr，逐行含换行）的 SHA-256 小写十六进制。
     * @property timedOut true=有界等待到期后脚本被 victim 内 watchdog 强制终止。
     */
    data class Ok(
        val exitCode: Int,
        val outputSha256: String,
        val timedOut: Boolean,
    ) : VictimFinishResult

    /**
     * 具名失败（协议/路径/超时/进程已关闭/管道断开）。
     *
     * @property retained true=失败路径保留了 victim 进程（A1 delta：finish 超时只
     *   detach/park、不 close/kill victim；仅显式 closeAll(非 rooted) 才允许终止）。
     */
    data class Failed(
        val reason: String,
        val retained: Boolean = false,
    ) : VictimFinishResult
}

/**
 * victim 行协议（R3f）：纯函数编解码，宿主可直接单测、不触进程。
 *
 * 协议（victim 侧固定 shell 循环，见 [ProcessVictim.DEFAULT_COMMAND]）：
 * - `probe` ⇒ 输出一行 `id -u`；
 * - `finish <timeout_s> '<scriptPath>'` ⇒ victim 内 fork/exec `/system/bin/sh <scriptPath>`，
 *   有界 watchdog 到点打 `[TIMEOUT_MARKER]` 并 kill 脚本，最后打 `[EXIT_MARKER_PREFIX]<code>`；
 * - `park` ⇒ victim 停读 stdin 并驻留（rooted child 不杀）；
 * - `bye` ⇒ victim 退出。
 *
 * @note 脚本路径由本对象的 [finishCommand] 校验并单引号包裹：只允许绝对路径、无换行/
 *   空白/单引号/超长；因此 victim shell 侧可以安全地做 `'...'` 去引号。
 * @note 阶段：R3f。
 */
internal object VictimProtocol {
    /** probe 命令 token。 */
    const val COMMAND_PROBE: String = "probe"

    /** park 命令 token（rooted child 驻留）。 */
    const val COMMAND_PARK: String = "park"

    /** bye 命令 token（victim 退出）。 */
    const val COMMAND_BYE: String = "bye"

    /** finish 命令前缀（后接 `<timeout_s> '<scriptPath>'`）。 */
    const val FINISH_PREFIX: String = "finish "

    /** 脚本退出码标记前缀（victim shell 最后输出）。 */
    const val EXIT_MARKER_PREFIX: String = "GLK_FINISH_EXIT:"

    /** 有界等待到期标记（victim 内 watchdog 在 kill 脚本前输出）。 */
    const val TIMEOUT_MARKER: String = "GLK_FINISH_TIMEOUT:1"

    /** 脚本路径最大长度（防止协议行异常长）。 */
    const val MAX_SCRIPT_PATH_LENGTH: Int = 4096

    /** 默认 finish 有界等待（毫秒）；与既有 `FinishExecution.DEFAULT_TIMEOUT_MS` 同值。 */
    const val DEFAULT_FINISH_TIMEOUT_MS: Long = 30_000L

    /** session 侧在 victim 内 watchdog 到期后再给的排空余量（毫秒）。 */
    const val FINISH_DRAIN_GRACE_MS: Long = 2_000L

    /**
     * 计算 victim shell 的 `sleep` 秒数（向上取整；至少 1 秒）。
     *
     * @param timeoutMs 有界等待（毫秒；必须 > 0）。
     * @return 秒数；参数非法返回 null。
     */
    fun timeoutSeconds(timeoutMs: Long): Long? {
        if (timeoutMs <= 0L) return null
        return if (timeoutMs >= Long.MAX_VALUE - 999L) {
            Long.MAX_VALUE / 1000L
        } else {
            (timeoutMs + 999L) / 1000L
        }
    }

    /**
     * 校验脚本路径（作为协议参数传给固定 shell 循环）。
     *
     * @param scriptPath 待校验路径。
     * @return null=合法；否则为具名子原因（调用方拼 `victim-finish:script-path:`）。
     */
    fun validateScriptPath(scriptPath: String): String? = when {
        scriptPath.isBlank() -> "blank"
        scriptPath.length > MAX_SCRIPT_PATH_LENGTH -> "too-long"
        !scriptPath.startsWith("/") -> "not-absolute"
        scriptPath.any { it == '\n' || it == '\r' || it == '\u0000' } -> "control-char"
        /* victim shell 以空白分隔 `<timeout_s> '<path>'`：路径含空白会破坏协议解析。 */
        scriptPath.any { it == ' ' || it == '\t' } -> "space-char"
        scriptPath.contains('\'') -> "quote-char"
        else -> null
    }

    /**
     * 构造 finish 协议行。
     *
     * @param timeoutMs 有界等待（毫秒；>0）。
     * @param scriptPath 脚本绝对路径（不含单引号/换行）。
     * @return `finish <seconds> '<path>'`；参数非法返回 null。
     */
    fun finishCommand(timeoutMs: Long, scriptPath: String): String? {
        if (validateScriptPath(scriptPath) != null) return null
        val seconds = timeoutSeconds(timeoutMs) ?: return null
        return FINISH_PREFIX + seconds.toString() + " '" + scriptPath + "'"
    }

    /**
     * 解析退出码标记行。
     *
     * @param line victim 输出的一行（不含换行）。
     * @return 退出码；不是标记行/数值非法返回 null。
     */
    fun parseExitMarker(line: String): Int? {
        if (!line.startsWith(EXIT_MARKER_PREFIX)) return null
        return line.substring(EXIT_MARKER_PREFIX.length).trim().toIntOrNull()
    }

    /**
     * @param line victim 输出的一行（不含换行）。
     * @return true=有界等待到期标记行。
     */
    fun isTimeoutMarker(line: String): Boolean = line == TIMEOUT_MARKER

    /**
     * 把一行脚本输出计入 SHA-256（含行尾换行，模拟 stdout 原始字节）。
     *
     * @param digest 正在累积的 SHA-256。
     * @param line 输出行（不含换行）。
     */
    fun updateDigest(digest: MessageDigest, line: String) {
        digest.update(line.toByteArray(Charsets.UTF_8))
        digest.update('\n'.code.toByte())
    }

    /**
     * @param bytes 任意字节。
     * @return SHA-256 小写十六进制（64 字符）。
     */
    fun sha256Hex(bytes: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
        val builder = StringBuilder(digest.size * 2)
        digest.forEach { byte ->
            val value = byte.toInt() and 0xFF
            builder.append(HEX_CHARS[value ushr 4])
            builder.append(HEX_CHARS[value and 0x0F])
        }
        return builder.toString()
    }

    /** SHA-256 十六进制字符集（小写）。 */
    private const val HEX_CHARS: String = "0123456789abcdef"
    /* ==================================================================== */
    /* G04 append-only：W3 self-probe 命令（status/comm-set/comm；既有语义不变） */
    /* ==================================================================== */

    const val COMMAND_STATUS: String = "status"
    const val COMMAND_COMM: String = "comm"
    const val COMMAND_COMM_SET_PREFIX: String = "comm-set "
    const val STATUS_MARKER_PREFIX: String = "GLK_STATUS:"
    const val COMM_MARKER_PREFIX: String = "GLK_COMM:"
    const val COMM_SET_MARKER_PREFIX: String = "GLK_COMM_SET:"
    const val MAX_STATUS_BYTES: Int = 4096
    const val MAX_COMM_BYTES: Int = 15

    /** @return `status` 命令行。 */
    fun statusCommand(): String = COMMAND_STATUS

    /** @return `comm` 命令行。 */
    fun commCommand(): String = COMMAND_COMM

    /**
     * @param marker 15B 内安全 ASCII。 @return `comm-set <marker>` 行；非法 null。
     */
    fun commSetCommand(marker: String): String? {
        if (marker.isEmpty() || marker.length > MAX_COMM_BYTES) return null
        if (marker.any { it.code < 0x20 || it.code > 0x7e || it == ' ' || it == '\'' }) return null
        return COMMAND_COMM_SET_PREFIX + marker
    }

    /**
     * @param line 输出行。 @return raw status（≤4 KiB）；格式/超限 null。
     */
    fun parseStatusLine(line: String): ByteArray? {
        val trimmed = line.trim()
        if (!trimmed.startsWith(STATUS_MARKER_PREFIX)) return null
        val hex = trimmed.removePrefix(STATUS_MARKER_PREFIX)
        if (hex.isEmpty() || hex.length % 2 != 0) return null
        if (hex.any { it !in "0123456789abcdef" }) return null
        val bytes = ByteArray(hex.length / 2)
        for (index in bytes.indices) {
            bytes[index] = ((hex[index * 2].digitToInt(16) shl 4) or hex[index * 2 + 1].digitToInt(16)).toByte()
        }
        return if (bytes.size <= MAX_STATUS_BYTES) bytes else null
    }

    /**
     * @param line 输出行。 @return len/bytes；格式非法 null。
     */
    fun parseCommLine(line: String): Pair<Int, ByteArray>? {
        val trimmed = line.trim()
        if (!trimmed.startsWith(COMM_MARKER_PREFIX)) return null
        val rest = trimmed.removePrefix(COMM_MARKER_PREFIX)
        val separator = rest.indexOf(':')
        if (separator <= 0) return null
        val length = rest.substring(0, separator).toIntOrNull() ?: return null
        if (length !in 0..MAX_COMM_BYTES) return null
        val hex = rest.substring(separator + 1)
        if (hex.length != length * 2 || hex.any { it !in "0123456789abcdef" }) return null
        val bytes = ByteArray(length)
        for (index in bytes.indices) {
            bytes[index] = ((hex[index * 2].digitToInt(16) shl 4) or hex[index * 2 + 1].digitToInt(16)).toByte()
        }
        return length to bytes
    }

    /**
     * @param line 输出行。 @return true=OK。
     */
    fun parseCommSetLine(line: String): Boolean =
        line.trim().removePrefix(COMM_SET_MARKER_PREFIX) == "OK" &&
            line.trim().startsWith(COMM_SET_MARKER_PREFIX)

}
