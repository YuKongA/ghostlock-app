package com.ghostlock.app.backend.cve98260.service

/**
 * 98260 service/payload 的固定路径（backend 自有契约；app/service 两侧共用，避免字符串漂移）。
 *
 * @note 这些是 shell 域可写路径，不是项目 finish payload 路径；finish 脚本内容由 UI 生成，
 *   由 `Cve98260RemoteExecutor` 暂存到 [FINISH_SCRIPT_PATH]。
 */
internal object Cve98260Paths {
    /** 暂存后的 exploit payload（可执行）。 */
    const val PAYLOAD_PATH: String = "/data/local/tmp/glk98260-helper"

    /** payload/service 日志（项目 shell 行也落这里）。 */
    const val LOG_PATH: String = "/data/local/tmp/glk98260.log"

    /** journal（跨清数据证据）。 */
    const val JOURNAL_PATH: String = "/data/local/tmp/glk98260.run"

    /** 启动脚本写下的 payload pid。 */
    const val PID_PATH: String = "/data/local/tmp/glk98260.pid"

    /** victim 启动脚本路径。 */
    const val LAUNCH_PATH: String = "/data/local/tmp/glk98260-launch.sh"

    /** UI 生成的 finish 脚本暂存路径（rooted victim 执行）。 */
    const val FINISH_SCRIPT_PATH: String = "/data/local/tmp/glk98260-finish.sh"

    /** SELinux enforce 读回路径。 */
    const val SELINUX_ENFORCE_PATH: String = "/sys/fs/selinux/enforce"
}
