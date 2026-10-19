package com.ghostlock.app.ui.util

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import com.ghostlock.app.ui.Natives
import com.topjohnwu.superuser.Shell
import java.io.InputStream
import java.util.concurrent.CompletableFuture
import java.util.concurrent.Executor
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit

/**
 *  KernelSU CLI 层的**未接线**替身（2026-10-18 恢复移植）。
 *
 *  真正的实现会通过 libsu 跑 ksud；这里一律返回固定样例，保证 UI 能跑、不触达系统。
 *  上游 KsuCli.kt 里被保留屏引用的入口（模板、sepolicy、模块动作、应用启停、重启等）
 *  全部保留同名同签名，实现换成固定值。
 */

/** ksud 的假路径（仅用于日志/展示）。 */
private const val DAEMON = "/data/adb/ksud"

/** 两个示例模块，字段与 KernelSU 的 module list JSON 对齐。 */
private val SAMPLE_MODULES = """
    [
      {"id":"sample-webui","name":"示例 WebUI 模块","version":"1.2.0","versionCode":120,
       "author":"GhostLock","description":"未接线占位：演示模块列表与 WebUI 入口","enabled":true,
       "remove":false},
      {"id":"sample-metamodule","name":"示例 MetaModule","version":"0.9.1","versionCode":91,
       "author":"GhostLock","description":"未接线占位：演示 metamodule 标记","enabled":false,
       "remove":false}
    ]
""".trimIndent()

/** 模块动作脚本的假输出（ExecuteModuleAction 屏原样展示）。 */
private const val SAMPLE_ACTION_OUTPUT = "[sample] module action is not wired in this build"

/** Shell 句柄（未接线）：永不启动进程、永不提权的空 [Shell]。 */
object KsuCli {
    val SHELL: Shell = NoopShell()
    val GLOBAL_MNT_SHELL: Shell = SHELL
}

/**
 * 空 Shell 实现：所有任务都被丢弃，状态恒为 [Shell.NON_ROOT_SHELL]。
 *
 * 目的：给保留屏提供类型正确、行为为空的句柄，避免真的 fork su/sh。
 */
private class NoopShell : Shell() {
    override fun isAlive(): Boolean = false

    override fun execTask(task: Shell.Task) {
        // 未接线：不执行任何命令。
    }

    override fun submitTask(task: Shell.Task) {
        // 未接线：不执行任何命令。
    }

    override fun newJob(): Shell.Job = NoopJob()

    override fun getStatus(): Int = Shell.NON_ROOT_SHELL

    override fun waitAndClose(timeout: Long, unit: TimeUnit): Boolean = true

    override fun close() {
        // 未接线：没有进程需要关闭。
    }
}

/** 空 Job：命令被忽略，exec 恒成功且无输出。 */
private class NoopJob : Shell.Job() {
    override fun to(out: MutableList<String>?): Shell.Job = this

    override fun to(out: MutableList<String>?, err: MutableList<String>?): Shell.Job = this

    override fun add(vararg cmds: String): Shell.Job = this

    override fun add(commands: InputStream): Shell.Job = this

    override fun exec(): Shell.Result = NoopResult()

    override fun submit(executor: Executor?, callback: Shell.ResultCallback?) {
        callback?.onResult(NoopResult())
    }

    override fun enqueue(): Future<Shell.Result> = CompletableFuture.completedFuture(NoopResult())
}

/** 空结果：无 stdout/stderr，退出码 0。 */
private class NoopResult : Shell.Result() {
    override fun getOut(): List<String> = emptyList()

    override fun getErr(): List<String> = emptyList()

    override fun getCode(): Int = 0
}

/**
 * 取一个 shell 句柄。
 *
 * @param globalMnt 上游用来区分 per-mount 命名空间；未接线时两者是同一个空句柄。
 * @return 空 [Shell]，不启动进程。
 */
fun getRootShell(globalMnt: Boolean = false): Shell =
    if (globalMnt) KsuCli.GLOBAL_MNT_SHELL else KsuCli.SHELL

/**
 * 在新 shell 里跑一段 lambda（上游语义）。
 *
 * @param globalMnt 同上，未接线时无区别。
 * @param block 以 [Shell] 为接收者的代码块。
 * @return block 的返回值。
 */
inline fun <T> withNewRootShell(
    globalMnt: Boolean = false,
    block: Shell.() -> T
): T {
    return getRootShell(globalMnt).block()
}

/**
 * 取一个新建的 shell（未接线：与 [getRootShell] 同一个空句柄）。
 *
 * @param globalMnt 同上。
 * @return 空 [Shell]。
 */
fun createRootShell(globalMnt: Boolean = false): Shell = getRootShell(globalMnt)

/**
 * 从 content URI 读显示名（Android API，不涉及 shell/网络）。
 *
 * @param context 用来查 ContentResolver。
 * @return 显示名；查不到时返回 null。
 */
fun Uri.getFileName(context: Context): String? {
    var fileName: String? = null
    val contentResolver: ContentResolver = context.contentResolver
    val cursor = contentResolver.query(this, null, null, null, null)
    cursor?.use {
        if (it.moveToFirst()) {
            fileName = it.getString(it.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
        }
    }
    return fileName
}

/**
 * 设备上有没有 root（未接线：假实现下恒有）。
 *
 * @return 恒为 true。
 */
fun rootAvailable(): Boolean = true

/**
 * 特性状态（未接线）。
 *
 * @param feature 特性名，被忽略。
 * @return 恒为 "unknown"。
 */
suspend fun getFeatureStatus(feature: String): String = "unknown"

/**
 * 特性持久值（未接线）。
 *
 * @param feature 特性名，被忽略。
 * @return 恒为 null。
 */
suspend fun getFeaturePersistValue(feature: String): Long? = null

/**
 * 特性当前值（未接线）。
 *
 * @param feature 特性名，被忽略。
 * @return 恒为 null。
 */
suspend fun getFeatureValue(feature: String): Long? = null

/**
 * 跑一条 ksud 命令（未接线：不执行）。
 *
 * @param args 命令行参数，被忽略。
 * @param newShell 是否新开 shell，被忽略。
 * @param globalMnt 是否全局挂载，被忽略。
 * @return 恒为 true。
 */
fun execKsud(args: String, newShell: Boolean = false, globalMnt: Boolean = false): Boolean = true

/**
 * 首次安装守护进程（未接线：什么都不做）。
 */
fun install() {
    // 未接线：不跑 ksud install。
}

/**
 * 模块清单（未接线：固定样例 JSON）。
 *
 * @return 两条示例模块的 JSON 数组。
 */
fun listModules(): String = SAMPLE_MODULES

/**
 * 模块数量（未接线：与样例一致）。
 *
 * @return 恒为 2。
 */
fun getModuleCount(): Int = 2

/**
 * 超级用户授权数量（走 Natives 假实现）。
 *
 * @return Natives 的固定值。
 */
fun getSuperuserCount(): Int = Natives.getSuperuserCount()

/**
 * 启用/禁用模块（未接线）。
 *
 * @param id 模块 id，被忽略。
 * @param enable 目标状态，被忽略。
 * @return 恒为 true。
 */
fun toggleModule(id: String, enable: Boolean): Boolean = true

/**
 * 撤销卸载标记（未接线）。
 *
 * @param id 模块 id，被忽略。
 * @return 恒为 true。
 */
fun undoUninstallModule(id: String): Boolean = true

/**
 * 卸载模块（未接线）。
 *
 * @param id 模块 id，被忽略。
 * @return 恒为 true。
 */
fun uninstallModule(id: String): Boolean = true

/**
 * 跑模块动作脚本（未接线：只吐一行固定样例）。
 *
 * @param moduleId 模块 id，仅用于日志。
 * @param onStdout 标准输出回调，会被调用一次。
 * @param onStderr 标准错误回调，未接线时不调用。
 * @return 恒为 true。
 */
fun runModuleAction(
    moduleId: String, onStdout: (String) -> Unit, onStderr: (String) -> Unit
): Boolean {
    onStdout(SAMPLE_ACTION_OUTPUT)
    return true
}

/**
 * 跑一次性脚本（未接线）。
 *
 * @param command 脚本命令，被忽略。
 * @param onStdout 标准输出回调，会被调用一次。
 * @param onStderr 标准错误回调，未接线时不调用。
 * @return 恒为 0（成功）。
 */
fun runOneshotScript(
    command: String, onStdout: (String) -> Unit, onStderr: (String) -> Unit
): Int {
    onStdout(SAMPLE_ACTION_OUTPUT)
    return 0
}

/**
 * 以 root 删除脚本解压目录（未接线：不删除任何文件）。
 *
 * @param path 目标路径，被忽略。
 * @return 恒为 true。
 */
fun removeAsRoot(path: String): Boolean = true

/**
 * 重启（未接线：不重启）。
 *
 * @param reason 重启目标（recovery / fastboot / soft_reboot），被忽略。
 */
fun reboot(reason: String = "") {
    // 未接线：不发 reboot 命令。
}

/**
 * 当前 KMI（未接线）。
 *
 * @return 固定样例 "unknown"。
 */
suspend fun getCurrentKmi(): String = "unknown"

/**
 * 支持的 KMI 列表（未接线）。
 *
 * @return 空列表。
 */
suspend fun getSupportedKmis(): List<String> = emptyList()

/**
 * 设备上有没有 Magisk（未接线）。
 *
 * @return 恒为 false。
 */
fun hasMagisk(): Boolean = false

/**
 * sepolicy 规则是否合法（未接线）。
 *
 * @param rules 规则文本；null 视为合法。
 * @return 恒为 true。
 */
fun isSepolicyValid(rules: String?): Boolean = true

/**
 * 取某个包的 sepolicy（未接线）。
 *
 * @param pkg 包名，被忽略。
 * @return 空字符串。
 */
fun getSepolicy(pkg: String): String = ""

/**
 * 设置某个包的 sepolicy（未接线）。
 *
 * @param pkg 包名，被忽略。
 * @param rules 规则文本，被忽略。
 * @return 恒为 true。
 */
fun setSepolicy(pkg: String, rules: String): Boolean = true

/**
 * 应用配置模板列表（未接线）。
 *
 * @return 空列表。
 */
fun listAppProfileTemplates(): List<String> = emptyList()

/**
 * 取一个应用配置模板（未接线）。
 *
 * @param id 模板 id，被忽略。
 * @return 空字符串。
 */
fun getAppProfileTemplate(id: String): String = ""

/**
 * 写入一个应用配置模板（未接线）。
 *
 * @param id 模板 id，被忽略。
 * @param template 模板内容，被忽略。
 * @return 恒为 true。
 */
fun setAppProfileTemplate(id: String, template: String): Boolean = true

/**
 * 删除一个应用配置模板（未接线）。
 *
 * @param id 模板 id，被忽略。
 * @return 恒为 true。
 */
fun deleteAppProfileTemplate(id: String): Boolean = true

/**
 * 强制停止应用（未接线）。
 *
 * @param packageName 包名，被忽略。
 * @param userId 用户 id，被忽略。
 */
fun forceStopApp(packageName: String, userId: Int? = null) {
    // 未接线：不发 am force-stop。
}

/**
 * 启动应用（未接线）。
 *
 * @param packageName 包名，被忽略。
 * @param userId 用户 id，被忽略。
 */
fun launchApp(packageName: String, userId: Int? = null) {
    // 未接线：不发 am start。
}

/**
 * 重启应用（未接线）。
 *
 * @param packageName 包名，被忽略。
 * @param userId 用户 id，被忽略。
 */
fun restartApp(packageName: String, userId: Int? = null) {
    // 未接线：不停止也不启动。
}

/** 守护进程路径（展示用）。 */
internal fun ksuDaemonPath(): String = DAEMON
