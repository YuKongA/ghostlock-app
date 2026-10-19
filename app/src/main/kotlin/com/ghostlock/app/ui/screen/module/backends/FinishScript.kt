package com.ghostlock.app.ui.screen.module.backends

/**
 * 把收尾选项收敛成一段可执行的 root 脚本。
 *
 * root 管理器沿用 remote/main 的做法：找到管理器自带的 ksud（libksud.so），
 * 解析 KMI 后执行 late-load；以后 ForkPatch 等只要在 rootManagers 里加 spec。
 */
fun FinishState.toRootScript(
    managerBinary: String?,
    stagedScript: String?,
    stagedLkm: String?,
): String = buildString {
    appendLine("#!/system/bin/sh")
    appendLine("set -u")
    appendLine("LOG=/data/local/tmp/.ghostlock_ksu.log")
    appendLine("log() { echo \"\$*\" | tee -a \"\$LOG\"; }")
    appendLine("log '[ghostlock] finish start'")
    if (rootManagerEnabled) {
        val spec = FinishConfig.spec(rootManager) ?: FinishConfig.rootManagers.first()
        appendLine("log '[ghostlock] start root manager: " + spec.name + "'")
        if (spec.launchOnly) {
            /* FolkPatch：KernelPatch 已加载时 root 由 apd 提供；这里只检查并拉起管理器。 */
            appendLine("if [ -x /data/adb/apd ]; then log '[ghostlock] apd present'; else log '[ghostlock] apd missing (KernelPatch not active)'; fi")
            appendLine("monkey -p " + spec.packages.first() + " -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true")
        } else {
            appendLine(if (managerBinary != null) "BIN=\"" + managerBinary + "\"" else "BIN=\"\"")
            spec.packages.forEach { pkg ->
                appendLine("if [ ! -x \"\$BIN\" ]; then BIN=\$(find /data/app -path '*/" + pkg + "*/lib/arm64/" + spec.binary + "' 2>/dev/null | head -1); fi")
            }
            appendLine("if [ -z \"\$BIN\" ]; then BIN=/data/local/tmp/" + spec.binary + "; fi")
            if (spec.binary == "libksud.so") {
                appendLine("if [ ! -x \"\$BIN\" ]; then BIN=/data/adb/ksu/bin/ksud; fi")
            }
            appendLine("log \"[ghostlock] loader=\$BIN\"")
            appendLine("if [ ! -x \"\$BIN\" ]; then log '[ghostlock] manager loader not found'; else")
            appendLine("  KMI=\$(uname -r | grep -oE 'android[0-9]+-[0-9]+\\.[0-9]+' | head -1)")
            appendLine("  \"\$BIN\" " + spec.args.joinToString(" ") + " --kmi \"\$KMI\" >>\"\$LOG\" 2>&1 || log '[ghostlock] manager late-load failed'")
            appendLine("fi")
        }
    }
    if (stagedLkm != null && (lkmEnabled || rootManager == "FolkPatch")) {
        if (rootManager == "FolkPatch") {
            /* 实测路径：FolkPatch 未安装 /data/adb/apd 时，从 APK 的 libapd.so 提取到
             * /data/local/tmp 再执行 late-load；apd 会做符号重定位、加载模块并写 marker。 */
            appendLine("log '[ghostlock] folkpatch jailbreak: load kernelpatch.ko'")
            appendLine("setenforce 0 2>/dev/null || echo 0 > /sys/fs/selinux/enforce 2>/dev/null || true")
            appendLine("APD=/data/adb/apd")
            appendLine("if [ ! -x \"\$APD\" ]; then")
            appendLine("  APK=\$(pm path me.yuki.folk 2>/dev/null | sed 's/package://')")
            appendLine("  SRC=\"\${APK%/*}/lib/arm64/libapd.so\"")
            appendLine("  if [ -r \"\$SRC\" ]; then")
            appendLine("    rm -f /data/local/tmp/ghostlock-apd 2>/dev/null || true")
            appendLine("    cp \"\$SRC\" /data/local/tmp/ghostlock-apd 2>/dev/null || true")
            appendLine("    [ -x /data/local/tmp/ghostlock-apd ] && APD=/data/local/tmp/ghostlock-apd")
            appendLine("  fi")
            appendLine("fi")
            appendLine("if [ -x \"\$APD\" ]; then")
            appendLine("  \"\$APD\" late-load --module \"" + stagedLkm + "\" --package-name me.yuki.folk >>\"\$LOG\" 2>&1 && log '[ghostlock] apd late-load ok' || log '[ghostlock] apd late-load failed'")
            appendLine("else")
            appendLine("  insmod \"" + stagedLkm + "\" && log '[ghostlock] insmod kernelpatch.ko ok' || log '[ghostlock] insmod kernelpatch.ko failed'")
            appendLine("fi")
            appendLine("mkdir -p /data/adb/ap 2>/dev/null || true")
            appendLine(": > /data/adb/ap/jailbreak 2>/dev/null || true")
            appendLine("am force-stop me.yuki.folk 2>/dev/null || true")
            appendLine("monkey -p me.yuki.folk -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true")
            appendLine("log '[ghostlock] folkpatch jailbreak marker set'")
        } else {
            appendLine("log '[ghostlock] load custom lkm'")
            appendLine("insmod \"" + stagedLkm + "\" " + lkmParams + " && log '[ghostlock] lkm loaded' || log '[ghostlock] lkm insmod failed'")
        }
    }
    if (lkmEnabled && stagedLkm != null && kernelAccessAfterRelay) {
        /* 43284：加载中继后开启内核内存访问接口。
         * Kotlin 侧 RelayAccessService 看到 ready 文件后打开 /dev/glk；
         * 插件连接即创建 connected 文件，关闭后删除；脚本最多等 5 秒。 */
        appendLine("ACCESS_READY=/data/local/tmp/ghostlock-lkm-ready")
        appendLine("ACCESS_CONNECTED=/data/local/tmp/ghostlock-lkm-connected")
        appendLine("rm -f \"\$ACCESS_READY\" \"\$ACCESS_CONNECTED\"")
        appendLine(": > \"\$ACCESS_READY\"")
        appendLine("i=0")
        appendLine("while [ \"\$i\" -lt 50 ] && [ ! -e \"\$ACCESS_CONNECTED\" ]; do sleep 0.1; i=\$((i+1)); done")
        appendLine("if [ -e \"\$ACCESS_CONNECTED\" ]; then")
        appendLine("  log '[ghostlock] kernel access connected; wait for close'")
        appendLine("  while [ -e \"\$ACCESS_CONNECTED\" ]; do sleep 0.2; done")
        appendLine("  log '[ghostlock] kernel access closed'")
        appendLine("else")
        appendLine("  log '[ghostlock] kernel access timeout; continue'")
        appendLine("fi")
        appendLine("rm -f \"\$ACCESS_READY\"")
    }
    if (scriptEnabled) {
        if (scriptMode == 0 && command.isNotBlank()) {
            appendLine("log '[ghostlock] run command'")
            appendLine(command)
        }
        if (scriptMode == 1 && stagedScript != null) {
            appendLine("log '[ghostlock] run script: " + stagedScript + "'")
            appendLine("sh \"" + stagedScript + "\" || log '[ghostlock] script failed'")
        }
    }
    if (runAppEnabled && appPackage.isNotBlank()) {
        appendLine("log '[ghostlock] launch app: " + appPackage + "'")
        appendLine("monkey -p " + appPackage + " -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p " + appPackage + " >/dev/null 2>&1")
    }
    if (restoreSelinux) {
        appendLine("log '[ghostlock] restore selinux enforcing'")
        appendLine("setenforce 1 2>/dev/null || echo 1 > /sys/fs/selinux/enforce")
    }
    if (lkmEnabled) {
    }
    if (softRebootAfterFinish) {
        appendLine("log '[ghostlock] soft reboot zygote'")
        appendLine("setprop ctl.restart zygote")
    }
    appendLine("log '[ghostlock] finish done'")
}
