package com.ghostlock.app.backend.cve98260.payload

/**
 * 98260 payload 启动脚本（backend-owned；由公用 `VictimScriptLauncher` 执行）。
 *
 * 脚本只做：清空/重定向日志、后台启动 payload、写 pid、立即 exit 0；payload 与 helper 解耦，
 * helper 的 exec 超时/close 不会触及 payload。
 */
/**
 * 98260 payload 启动脚本（由 helper `execScript` 执行；**秒级返回**）。
 *
 * 脚本只做：清空/重定向日志、后台启动 payload、写 pid、立即 exit 0。payload 与 helper 解耦，
 * helper 的 exec 超时/close 不会触及 payload。
 */
internal object Cve98260LaunchScript {
    /**
     * @param payloadPath payload 绝对路径（可执行）。
     * @param params profile 参数串（已由 parser 校验字符集）。
     * @param finishScript 收尾脚本路径。
     * @param runId run id。
     * @param logPath payload 日志路径。
     * @param pidPath pid 文件路径。
     * @return 脚本 UTF-8 文本。
     */
    internal fun render(
        payloadPath: String,
        params: String,
        finishScript: String,
        runId: String,
        logPath: String,
        pidPath: String,
    ): String = buildString {
        appendLine("#!/system/bin/sh")
        appendLine("set -u")
        appendLine(": > " + quote(logPath))
        appendLine("chmod 600 " + quote(logPath) + " 2>/dev/null || true")
        append(
            quote(payloadPath) + " --params " + quote(params) + " --mode fake" +
                " --finish-script " + quote(finishScript) + " --run-id " + quote(runId) +
                " </dev/null >>" + quote(logPath) + " 2>&1 &\n",
        )
        appendLine("echo $! > " + quote(pidPath))
        appendLine("exit 0")
    }

    /** @return 单引号 shell 字面量（内部 `'` 断开）。 */
    private fun quote(value: String): String = "'" + value.replace("'", "'\\''") + "'"
}
