package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.net.Uri
import com.ghostlock.app.jni.NativeFinish
import java.io.File
import java.util.Base64

/**
 * 收尾参数（不在 Kotlin 里拼 shell 脚本）。
 *
 * 参数以 key=value 行写入 files/finish/finish.params，root_cmd 读取后直接执行。
 * NativeFinish.writeFile 是 JNI 入口；native 不可用时回退到普通文件写入。
 */
object FinishParams {
    private const val FILE_NAME = "finish.params"

    fun save(context: Context, state: FinishState): File {
        val dir = (context.getExternalFilesDir(null)?.resolve("finish") ?: File(context.filesDir, "finish")).apply { mkdirs() }
        val managerPrivate = FinishScriptStore.stageManagerBinary(context, state.rootManager)
        val managerBinary = managerPrivate?.let { path ->
            val dest = File(dir, "root-" + File(path).name)
            runCatching {
                File(path).inputStream().use { input -> dest.outputStream().use { output -> input.copyTo(output) } }
                dest.setExecutable(true, false)
                dest.absolutePath
            }.getOrNull()
        }
        val manualLkm = state.lkmUri?.let { stage(context, Uri.parse(it), File(dir, "custom.ko")) }
        val autoFolkPatchKo = if (state.rootManager == "FolkPatch") FolkPatchKo.extract(context) else null
        val lkm = autoFolkPatchKo ?: manualLkm
        val script = state.scriptUri?.let { stage(context, Uri.parse(it), File(dir, "script.sh")) }
        val lines = buildList {
            add("manager=" + state.rootManager)
            add("manager_enabled=" + if (state.rootManagerEnabled) 1 else 0)
            add("manager_binary=" + rootPath(managerBinary))
            add("app_enabled=" + if (state.runAppEnabled) 1 else 0)
            add("app_package=" + state.appPackage)
            add("command_enabled=" + if (state.scriptEnabled) 1 else 0)
            add("command_mode=" + state.scriptMode)
            add("command_b64=" + Base64.getEncoder().encodeToString(state.command.toByteArray()))
            add("script_path=" + rootPath(script))
            add("lkm_enabled=" + if (state.lkmEnabled) 1 else 0)
            add("lkm_path=" + rootPath(lkm))
            add("restore_selinux=" + if (state.restoreSelinux) 1 else 0)
            add("soft_reboot=" + if (state.softRebootAfterFinish) 1 else 0)
            add("kernel_access_after_relay=" + if (state.kernelAccessAfterRelay) 1 else 0)
        }
        val content = lines.joinToString("\n", postfix = "\n")
        val file = File(dir, FILE_NAME)
        val nativeOk = runCatching { NativeFinish.write(file.absolutePath, content) }.getOrDefault(-1) == 0
        if (!nativeOk) file.writeText(content)
        return file
    }

    private fun rootPath(path: String?): String =
        path.orEmpty().replace("/storage/emulated/0/", "/data/media/0/")

    private fun stage(context: Context, uri: Uri, dest: File): String? = runCatching {
        context.contentResolver.openInputStream(uri)?.use { input ->
            dest.outputStream().use { output -> input.copyTo(output) }
        }
        dest.absolutePath
    }.getOrNull()
}
