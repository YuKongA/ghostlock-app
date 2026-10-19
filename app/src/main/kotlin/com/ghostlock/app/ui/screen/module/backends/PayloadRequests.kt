package com.ghostlock.app.ui.screen.module.backends

/** 载荷脚本的上下文：所有已解析/已暂存的外部路径。 */
data class PayloadContext(
    val state: FinishState,
    val managerBinary: String?,
    val stagedScript: String?,
    val stagedLkm: String?,
)

/** 一种载荷。四种实现：root 管理器、应用、命令、LKM。 */
interface PayloadRequest {
    val id: String
    fun appendTo(line: (String) -> Unit, context: PayloadContext)
}

/** 启动 root 管理器。 */
class RootManagerPayload : PayloadRequest {
    override val id: String = "root"

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        val state = context.state
        if (!state.rootManagerEnabled) return
        val spec = FinishConfig.spec(state.rootManager) ?: FinishConfig.rootManagers.first()
        line("log '[ghostlock] start root manager: " + spec.name + "'")
        if (spec.launchOnly) {
            line("if [ -x /data/adb/apd ]; then log '[ghostlock] apd present'; else log '[ghostlock] apd missing (KernelPatch not active)'; fi")
            line("monkey -p " + spec.packages.first() + " -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true")
            return
        }
        line(if (context.managerBinary != null) "BIN=\"" + context.managerBinary + "\"" else "BIN=\"\"")
        spec.packages.forEach { pkg ->
            line("if [ ! -x \"\$BIN\" ]; then BIN=\$(find /data/app -path '*/" + pkg + "*/lib/arm64/" + spec.binary + "' 2>/dev/null | head -1); fi")
        }
        line("if [ -z \"\$BIN\" ]; then BIN=/data/local/tmp/" + spec.binary + "; fi")
        if (spec.binary == "libksud.so") {
            line("if [ ! -x \"\$BIN\" ]; then BIN=/data/adb/ksu/bin/ksud; fi")
        }
        line("log \"[ghostlock] loader=\$BIN\"")
        line("if [ ! -x \"\$BIN\" ]; then log '[ghostlock] manager loader not found'; else")
        line("  KVER=\$(uname -r | cut -d. -f1-2); AVER=\$(uname -r | grep -o 'android[0-9]*' | head -1); KMI=\"\$AVER-\$KVER\"")
        line("  \"\$BIN\" " + spec.args.joinToString(" ") + " --kmi \"\$KMI\" >>\"\$LOG\" 2>&1 || log '[ghostlock] manager late-load failed'")
        line("  \"\$BIN\" post-fs-data >>\"\$LOG\" 2>&1 || true")
        line("  \"\$BIN\" services >>\"\$LOG\" 2>&1 || true")
        line("  \"\$BIN\" boot-completed >>\"\$LOG\" 2>&1 || true")
        line("fi")
    }
}

/** 在 root 脚本里启动一个应用。 */
class AppPayload : PayloadRequest {
    override val id: String = "app"

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        val state = context.state
        if (!state.runAppEnabled || state.appPackage.isBlank()) return
        line("log '[ghostlock] launch app: " + state.appPackage + "'")
        line("monkey -p " + state.appPackage + " -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p " + state.appPackage + " >/dev/null 2>&1")
    }
}

/** 执行一条命令或一个脚本。 */
class CommandPayload : PayloadRequest {
    override val id: String = "cmd"

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        val state = context.state
        if (!state.scriptEnabled) return
        if (state.scriptMode == 0 && state.command.isNotBlank()) {
            line("log '[ghostlock] run command'")
            line(state.command)
        }
        if (state.scriptMode == 1 && context.stagedScript != null) {
            line("log '[ghostlock] run script: " + context.stagedScript + "'")
            line("sh \"" + context.stagedScript + "\" || log '[ghostlock] script failed'")
        }
    }
}

/** 加载自定义 LKM；FolkPatch 的管理器模块也走这里。 */
class LkmPayload : PayloadRequest {
    override val id: String = "lkm"

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        val state = context.state
        val lkm = context.stagedLkm ?: return
        if (!state.lkmEnabled && state.rootManager != "FolkPatch") return
        if (state.rootManager == "FolkPatch") {
            line("log '[ghostlock] folkpatch jailbreak: load kernelpatch.ko'")
            line("setenforce 0 2>/dev/null || echo 0 > /sys/fs/selinux/enforce 2>/dev/null || true")
            line("APD=/data/adb/apd")
            line("if [ ! -x \"\$APD\" ]; then")
            line("  APK=\$(pm path me.yuki.folk 2>/dev/null | sed 's/package://')")
            line("  SRC=\"\${APK%/*}/lib/arm64/libapd.so\"")
            line("  if [ -r \"\$SRC\" ]; then")
            line("    rm -f /data/local/tmp/ghostlock-apd 2>/dev/null || true")
            line("    cp \"\$SRC\" /data/local/tmp/ghostlock-apd 2>/dev/null || true")
            line("    [ -x /data/local/tmp/ghostlock-apd ] && APD=/data/local/tmp/ghostlock-apd")
            line("  fi")
            line("fi")
            line("mkdir -p /data/adb/ap 2>/dev/null || true")
            line(": > /data/adb/ap/jailbreak 2>/dev/null || true")
            line("log '[ghostlock] folkpatch jailbreak marker set'")
            line("if [ -x \"\$APD\" ]; then")
            line("  ( \"\$APD\" late-load --module \"" + lkm + "\" --package-name me.yuki.folk >>\"\$LOG\" 2>&1 & )")
            line("  log '[ghostlock] apd late-load started'")
            line("  sleep 6")
            line("else")
            line("  insmod \"" + lkm + "\" && log '[ghostlock] insmod kernelpatch.ko ok' || log '[ghostlock] insmod kernelpatch.ko failed'")
            line("fi")
            line("nohup sh -c 'sleep 8; am force-stop me.yuki.folk 2>/dev/null || true; monkey -p me.yuki.folk -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true' >/dev/null 2>&1 &")
        } else {
            line("log '[ghostlock] load custom lkm'")
            line("insmod \"" + lkm + "\" " + state.lkmParams + " && log '[ghostlock] lkm loaded' || log '[ghostlock] lkm insmod failed'")
        }
        if (state.lkmEnabled && state.kernelAccessAfterRelay) {
            line("ACCESS_DEV=/dev/glk")
            line("i=0")
            line("while [ \"\$i\" -lt 50 ] && [ ! -e \"\$ACCESS_DEV\" ]; do sleep 0.1; i=\$((i+1)); done")
            line("if [ -e \"\$ACCESS_DEV\" ]; then")
            line("  log '[ghostlock] kernel access device ready; wait for close'")
            line("  i=0")
            line("  while [ -e \"\$ACCESS_DEV\" ] && [ \"\$i\" -lt 600 ]; do sleep 0.2; i=\$((i+1)); done")
            line("  log '[ghostlock] kernel access closed'")
            line("else")
            line("  log '[ghostlock] kernel access timeout; continue'")
            line("fi")
        }
    }
}

/** 按 state.order 生成四种载荷的列表；未知 id 忽略。 */
fun FinishState.payloadRequests(context: PayloadContext): List<PayloadRequest> {
    val available = listOf(RootManagerPayload(), AppPayload(), CommandPayload(), LkmPayload())
    val byId = available.associateBy { it.id }
    val ordered = (order + available.map { it.id }).distinct().mapNotNull { byId[it] }
    return ordered.filter { payload ->
        when (payload.id) {
            "root" -> rootManagerEnabled
            "app" -> runAppEnabled && appPackage.isNotBlank()
            "cmd" -> scriptEnabled
            "lkm" -> context.stagedLkm != null && (lkmEnabled || rootManager == "FolkPatch")
            else -> false
        }
    }
}
