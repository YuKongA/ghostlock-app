package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.net.Uri
import java.io.File
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** 收尾选项：最终都收敛为「以 root 执行一段收尾脚本」。 */
data class FinishState(
    val rootManagerEnabled: Boolean = true,
    val rootManager: String = "KernelSU",
    val runAppEnabled: Boolean = false,
    val appPackage: String = "",
    val scriptEnabled: Boolean = false,
    val scriptMode: Int = 0,
    val command: String = "",
    val scriptUri: String? = null,
    val lkmEnabled: Boolean = false,
    val lkmUri: String? = null,
    val lkmParams: String = "",
    val restoreSelinux: Boolean = false,
    /** 43284：加载中继 LKM 后开启内核内存访问接口（等待插件连接 5 秒）。 */
    val kernelAccessAfterRelay: Boolean = false,
    /** 收尾脚本最后重启 zygote（软重启）。 */
    val softRebootAfterFinish: Boolean = false,
    val order: List<String> = DEFAULT_ORDER,
)

/**
 *  收尾配置（2026-10-18 接线）。
 *
 *  UI 上是四类选项；交给后端时收敛成一个 root 脚本 + 可选 lkm 路径：
 *   · 启动 root 管理器 → insmod <manager.ko>
 *   · 以 root 执行应用 → am start / app_process 包装（由脚本决定）
 *   · 以 root 执行自定义脚本 → sh <script>
 *   · 加载自定义 lkm → insmod <lkm>
 */
val DEFAULT_ORDER = listOf("root", "app", "cmd", "lkm", "selinux", "reboot")

object FinishConfig {
    private const val PREFS = "ghostlock.finish"
    private const val KEY = "state"

    var state by mutableStateOf(FinishState())
        private set

    /**
     * root 管理器描述：包名列表 + 管理器自带的加载器二进制 + 参数。
     *
     * 以后加 ForkPatch 等只需新增一条 spec（不同包名/二进制/参数），
     * 脚本生成与 UI 都不需要改。
     */
    data class RootManagerSpec(
        val name: String,
        val packages: List<String>,
        val binary: String,
        val args: List<String>,
        /** true = 管理器本身不提供 ksud late-load，root 由已加载的 KernelPatch/apd 提供。 */
        val launchOnly: Boolean = false,
    )

    val rootManagers = listOf(
        RootManagerSpec("KernelSU", listOf("me.weishu.kernelsu"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("KernelSU Next", listOf("me.weishu.kernelsu.pr"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("ReSukiSU", listOf("com.resukisu.resukisu"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("SuperManager", listOf("com.kowx712.supermanager"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("FolkPatch", listOf("me.yuki.folk", "me.bmax.apatch"), "", emptyList(), launchOnly = true),
    )

    fun spec(name: String): RootManagerSpec? = rootManagers.firstOrNull { it.name == name }

    /** 管理器是否已安装（任一包名存在即算）。 */
    fun installedPackages(context: Context, spec: RootManagerSpec): List<String> =
        spec.packages.filter { pkg -> runCatching { context.packageManager.getPackageInfo(pkg, 0) }.isSuccess }

    fun load(context: Context) {
        val raw = context.getSharedPreferences(PREFS, 0).getString(KEY, null) ?: return
        val parts = raw.split("|")
        if (parts.size < 8) return
        state = FinishState(
            rootManagerEnabled = parts[0] == "1",
            rootManager = parts[1],
            runAppEnabled = parts[2] == "1",
            appPackage = parts[3],
            scriptEnabled = parts[4] == "1",
            scriptMode = parts.getOrElse(9) { "0" }.toIntOrNull() ?: 0,
            command = parts.getOrElse(10) { "" },
            scriptUri = parts[5].ifBlank { null },
            lkmEnabled = parts[6] == "1",
            lkmUri = parts[7].ifBlank { null },
            lkmParams = parts.getOrElse(8) { "" },
            restoreSelinux = parts.getOrElse(11) { "0" } == "1",
            kernelAccessAfterRelay = parts.getOrElse(12) { "0" } == "1",
            softRebootAfterFinish = parts.getOrElse(13) { "0" } == "1",
            order = parts.getOrElse(14) { "" }.split(',').filter { it.isNotBlank() }.ifEmpty {
                val legacy = parts.getOrElse(13) { "" }
                if (legacy.contains(',')) legacy.split(',').filter { it.isNotBlank() } else DEFAULT_ORDER
            },
        ).let { loaded ->
            loaded.copy(order = loaded.order + DEFAULT_ORDER.filter { id -> id !in loaded.order })
        }
    }

    fun update(context: Context, value: FinishState) {
        state = value
        val raw = listOf(
            if (value.rootManagerEnabled) "1" else "0",
            value.rootManager,
            if (value.runAppEnabled) "1" else "0",
            value.appPackage,
            if (value.scriptEnabled) "1" else "0",
            value.scriptUri.orEmpty(),
            if (value.lkmEnabled) "1" else "0",
            value.lkmUri.orEmpty(),
            value.lkmParams,
            value.scriptMode.toString(),
            value.command,
            if (value.restoreSelinux) "1" else "0",
            if (value.kernelAccessAfterRelay) "1" else "0",
            if (value.softRebootAfterFinish) "1" else "0",
            value.order.joinToString(","),
        ).joinToString("|")
        context.getSharedPreferences(PREFS, 0).edit().putString(KEY, raw).apply()
    }
}


