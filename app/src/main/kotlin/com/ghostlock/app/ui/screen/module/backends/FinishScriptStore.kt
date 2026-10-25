package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import java.io.File

/**
 * 收尾脚本文件：`files/finish/run.sh`。
 *
 * 只做三件事：写当前 run 的脚本、给预览写一份、完成后删除。日志文件由后续执行层
 * 生成与转写，本对象不碰。
 */
object FinishScriptStore {
    /** 固定路径；panic 残留由下一轮启动删除。 */
    fun scriptFile(context: Context): File = File(File(context.filesDir, "finish"), "run.sh")

    /**
     * 覆盖写脚本。
     *
     * @return 写入的文件。
     * @throws IllegalStateException 目录创建失败（调用方按 `prep-failed` 处理）。
     */
    fun write(context: Context, script: String): File {
        val file = scriptFile(context)
        val parent = file.parentFile
        if (parent != null && !parent.isDirectory && !parent.mkdirs()) {
            throw IllegalStateException("finish-dir")
        }
        file.writeText(script)
        return file
    }

    /** 删除脚本；日志保留。 */
    fun delete(context: Context) {
        runCatching { scriptFile(context).delete() }
    }

    /** 预览：写一份带 `LOG=/dev/null` 的脚本，不启动攻击。 */
    fun preview(context: Context, state: FinishState): File =
        write(context, state.toRootScript(null, emptyMap(), "/dev/null"))
}
