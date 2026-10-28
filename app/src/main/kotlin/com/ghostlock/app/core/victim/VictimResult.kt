package com.ghostlock.app.core.victim

/**
 * 通用 victim 组件的具名结果：成功值或失败原因（不含业务成功主张）。
 *
 * @param T 成功值类型。
 * @note 失败 reason 必须稳定可检索（`victim-*`），不得为空；不保存异常对象，
 *   避免泄露内部实现/堆栈。
 * @note 阶段：批 A（task-52）。
 */
internal sealed interface VictimResult<out T> {
    /**
     * 成功。
     *
     * @property value 结果值。
     */
    data class Ok<T>(val value: T) : VictimResult<T>

    /**
     * 失败。
     *
     * @property reason 具名原因（稳定可检索）。
     */
    data class Failed(val reason: String) : VictimResult<Nothing>

    companion object {
        /**
         * @param reason 具名原因（非空）。
         * @return 失败结果。
         */
        fun <T> failed(reason: String): VictimResult<T> {
            require(reason.isNotBlank()) { "victim-result:blank-reason" }
            return Failed(reason)
        }
    }
}

/**
 * 通用 victim 组件的错误码（稳定 token；`VictimResult.Failed.reason` 使用）。
 *
 * @property token 稳定可检索文本。
 * @note 只覆盖机械/传输层错误；业务判定（uid/root/地址投票）不在此列。
 * @note 阶段：批 A。
 */
internal enum class VictimError(val token: String) {
    /** 会话/进程未启动或已关闭。 */
    NOT_STARTED("victim:not-started"),

    /** 能力集未启用该命令。 */
    CAPABILITY_DISABLED("victim:capability-disabled"),

    /** helper 启动（posix_spawn）失败。 */
    SPAWN_FAILED("victim:spawn-failed"),

    /** 控制帧编解码/校验失败。 */
    BAD_FRAME("victim:bad-frame"),

    /** 控制通道写失败。 */
    SEND_FAILED("victim:send-failed"),

    /** 控制通道读超时。 */
    TIMEOUT("victim:timeout"),

    /** 控制通道 EOF/子进程退出。 */
    EOF("victim:eof"),

    /** helper 返回 ERROR 状态（带 helper error code/errno）。 */
    HELPER_ERROR("victim:helper-error"),

    /** raw 通道长度/头/截断校验失败。 */
    RAW_INVALID("victim:raw-invalid"),

    /** raw 通道读失败。 */
    RAW_READ_FAILED("victim:raw-read-failed"),

    /** waitpid/reap 失败。 */
    WAIT_FAILED("victim:wait-failed"),

    /** kill 失败。 */
    KILL_FAILED("victim:kill-failed"),

    /** 身份读取失败（`VictimIdentityException.reason`）。 */
    IDENTITY_FAILED("victim:identity-failed"),

    /** 参数域非法（越界/超长/为空）。 */
    INVALID_ARGUMENT("victim:invalid-argument"),
}
