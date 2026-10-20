package com.ghostlock.app.ui.screen.module.backends

/**
 * 载荷脚本的上下文：已解析/已暂存的外部路径。
 *
 * @property staged slot.id 到真实路径（脚本 / .ko）的映射；同类载荷可有多条，各自解析。
 * @property folkPatchLkm rootManager=FolkPatch 且没有 LKM 槽位时的自动模块路径。
 */
data class PayloadContext(
    val state: FinishState,
    val managerBinary: String?,
    val staged: Map<String, String> = emptyMap(),
    val folkPatchLkm: String? = null,
)

/** 一种载荷。四种实现：root 管理器、应用、命令、LKM。 */
interface PayloadRequest {
    /** 本请求对应的载荷实例。 */
    val slot: PayloadSlot

    fun appendTo(line: (String) -> Unit, context: PayloadContext)
}

/** 启动 root 管理器。 */
class RootManagerPayload(override val slot: PayloadSlot) : PayloadRequest {

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        val spec = FinishConfig.spec(context.state.rootManager) ?: FinishConfig.rootManagers.first()
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
class AppPayload(override val slot: PayloadSlot) : PayloadRequest {

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        if (slot.appPackage.isBlank()) return
        line("log '[ghostlock] launch app: " + slot.appPackage + "'")
        line("monkey -p " + slot.appPackage + " -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p " + slot.appPackage + " >/dev/null 2>&1")
    }
}

/** 执行一条命令或一个脚本。 */
class CommandPayload(override val slot: PayloadSlot) : PayloadRequest {

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        if (slot.scriptMode == 0 && slot.command.isNotBlank()) {
            line("log '[ghostlock] run command'")
            line(slot.command)
        }
        if (slot.scriptMode == 1) {
            val staged = context.staged[slot.id] ?: return
            line("log '[ghostlock] run script: " + staged + "'")
            line("sh \"" + staged + "\" || log '[ghostlock] script failed'")
        }
    }
}

/** 加载自定义 LKM；FolkPatch 的管理器模块也走这里。 */
class LkmPayload(
    override val slot: PayloadSlot,
    private val stagedLkm: String,
) : PayloadRequest {

    override fun appendTo(line: (String) -> Unit, context: PayloadContext) {
        val state = context.state
        val lkm = stagedLkm
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
            line("insmod \"" + lkm + "\" " + slot.lkmParams + " && log '[ghostlock] lkm loaded' || log '[ghostlock] lkm insmod failed'")
        }
        if (state.kernelAccessAfterRelay) {
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

/**
 * 按 [FinishState.payloads] 的顺序生成载荷列表：同一条实例出现几次就生成几次。
 *
 * 没有执行价值的实例（app 没选包名、命令为空、脚本/LKM 没解析出路径）直接跳过，
 * 但不改动用户表里的条目 —— 编辑界面仍然看得到、可以补参数。
 *
 * @param context 已解析路径的上下文。
 * @return 可直接渲染进脚本的载荷，顺序即执行顺序。
 */
fun FinishState.payloadRequests(context: PayloadContext): List<PayloadRequest> = buildList {
    payloads.filter { it.enabled }.forEach { slot ->
        when (slot.kind) {
            PayloadKind.Root -> add(RootManagerPayload(slot))
            PayloadKind.App -> if (slot.appPackage.isNotBlank()) add(AppPayload(slot))
            PayloadKind.Command -> {
                val hasScript = slot.scriptMode == 1 && context.staged[slot.id] != null
                if (hasScript || (slot.scriptMode == 0 && slot.command.isNotBlank())) add(CommandPayload(slot))
            }
            PayloadKind.Lkm -> context.staged[slot.id]?.let { add(LkmPayload(slot, it)) }
        }
    }
    /* FolkPatch 的管理器模块不属于用户载荷：没有「已配置 ko」的 LKM 槽位时补一条自动加载。
     * 注意判据是“能用”而不是“存在”：启用了但没选文件的槽不产出语句，不能因此压掉自动模块。 */
    if (rootManager == "FolkPatch" && payloads.none {
            it.enabled && it.kind == PayloadKind.Lkm && !it.lkmUri.isNullOrBlank()
        }
    ) {
        context.folkPatchLkm?.let { add(LkmPayload(PayloadSlot("slot-folk", PayloadKind.Lkm), it)) }
    }
}
