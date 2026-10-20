package com.ghostlock.app.ui.screen.module.backends

import java.io.File

/** 单个载荷/收尾项的效果判定；没有观测到就是 [Unknown]，不伪造成功。 */
enum class EffectState { Verified, Failed, Unknown }

/** 一条具名结果：item 是载荷 id（root/app/cmd/lkm/...），detail 是可核对的依据。 */
data class EffectReport(val item: String, val state: EffectState, val detail: String)

/**
 * 攻击后的效果探针。
 *
 * 只读内核视图与既有文件，不写任何东西；判定口径：
 *  - KSU：`su` 或 `/sys/module/kernelsu`；
 *  - 自定义 LKM：`/proc/modules` 或 `/sys/module/<name>`；
 *  - app / cmd：没有可靠探针时记 [EffectState.Unknown]；
 *  - SELinux：`/sys/fs/selinux/enforce` 可读时按 1/0 判，读不到记 Unknown。
 */
object PayloadEffects {
    fun evaluate(
        state: FinishState,
        exists: (String) -> Boolean = { File(it).exists() },
        modules: () -> String? = {
            runCatching { File("/proc/modules").readText() }.getOrNull()
        },
        enforcing: () -> Boolean? = {
            runCatching { File("/sys/fs/selinux/enforce").readText().trim() == "1" }.getOrNull()
        },
    ): List<EffectReport> = buildList {
        /* 载荷实例逐条判定：同类出现几次就有几条报告（2026-10-19）。 */
        state.payloads.filter { it.enabled }.forEach { slot ->
            when (slot.kind) {
                PayloadKind.Root -> {
                    val spec = FinishConfig.spec(state.rootManager)
                    if (spec?.launchOnly == true) {
                        val ok = exists("/data/adb/apd") || exists("/system/bin/su")
                        add(EffectReport("root", if (ok) EffectState.Verified else EffectState.Unknown, state.rootManager))
                    } else {
                        val ok = exists("/system/bin/su") || exists("/system/xbin/su") ||
                            moduleLoaded(modules(), "kernelsu")
                        add(EffectReport("root", if (ok) EffectState.Verified else EffectState.Unknown, state.rootManager))
                    }
                }
                PayloadKind.App -> {
                    val detail = slot.appPackage.ifBlank { "blank" }
                    add(EffectReport("app", if (slot.appPackage.isBlank()) EffectState.Failed else EffectState.Unknown, detail))
                }
                PayloadKind.Command -> {
                    add(EffectReport("cmd", EffectState.Unknown, if (slot.scriptMode == 0) "inline" else "script"))
                }
                PayloadKind.Lkm -> {
                    val name = moduleName(slot.lkmUri)
                    val ok = name != null && (moduleLoaded(modules(), name) || exists("/sys/module/" + name))
                    add(EffectReport("lkm", if (ok) EffectState.Verified else EffectState.Unknown, name ?: "path"))
                }
            }
        }
        if (state.restoreSelinux) {
            val result = enforcing()
            val effect = when (result) {
                true -> EffectState.Verified
                false -> EffectState.Failed
                null -> EffectState.Unknown
            }
            add(EffectReport("selinux", effect, result?.toString() ?: "unreadable"))
        }
        if (state.softRebootAfterFinish) {
            add(EffectReport("reboot", EffectState.Unknown, "zygote"))
        }
    }

    /** 汇总：任一 Failed 即 Failed；全 Verified 才 Verified；其余 Unknown。 */
    fun overall(reports: List<EffectReport>): EffectState = when {
        reports.any { it.state == EffectState.Failed } -> EffectState.Failed
        reports.isEmpty() -> EffectState.Unknown
        reports.all { it.state == EffectState.Verified } -> EffectState.Verified
        else -> EffectState.Unknown
    }

    private fun moduleLoaded(modules: String?, name: String): Boolean =
        modules?.lineSequence()?.any { line -> line.startsWith(name + " ") } == true

    /** `.../foo.ko` → `foo`；无路径（content:// 已进 memfd）返回 null。 */
    private fun moduleName(uri: String?): String? {
        val file = uri?.substringAfterLast('/') ?: return null
        if (!file.endsWith(".ko")) return null
        return file.removeSuffix(".ko").takeIf { it.isNotBlank() }
    }
}
