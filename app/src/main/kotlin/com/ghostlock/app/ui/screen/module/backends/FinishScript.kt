package com.ghostlock.app.ui.screen.module.backends

/**
 * 把收尾选项收敛成**一整段** root 脚本：四类载荷 + 收尾，全部在内存生成。
 *
 * 脚本由 [FinishScriptStore] 写到 `files/finish/run.sh`；LKM 只拿路径并以
 * `sh <路径>` 执行。日志通过 `LOG` 指向 App 的 memfd（`/proc/<pid>/fd/<n>`），
 * 子进程零文件写入。
 *
 * 顺序即执行顺序：先按 [FinishState.order] 跑载荷，再按开关追加收尾语句。
 */
fun FinishState.toRootScript(
    managerBinary: String?,
    stagedScript: String?,
    stagedLkm: String?,
    logPath: String = "/dev/null",
): String = buildString {
    appendLine("#!/system/bin/sh")
    appendLine("set -u")
    appendLine("LOG=" + shQuote(logPath))
    appendLine("exec 3>>\"\$LOG\"")
    appendLine("log() { printf '%s\\n' \"\$*\" >&3; }")
    appendLine("log 'finish start'")

    val context = PayloadContext(this@toRootScript, managerBinary, stagedScript, stagedLkm)
    payloadRequests(context).forEach { payload ->
        payload.appendTo(::appendLine, context)
    }

    if (restoreSelinux) {
        appendLine("log 'restore selinux enforcing'")
        appendLine("setenforce 1 2>/dev/null || echo 1 > /sys/fs/selinux/enforce")
    }
    if (softRebootAfterFinish) {
        appendLine("log 'soft reboot zygote'")
        appendLine("setprop ctl.restart zygote")
    }
    appendLine("log 'finish done'")
}

/**
 * 单引号包裹；内部单引号按 shell 规则断开。
 *
 * @param value 任意文本。
 * @return 可直接拼进脚本的字面量。
 */
fun shQuote(value: String): String = "'" + value.replace("'", "'\\''") + "'"
