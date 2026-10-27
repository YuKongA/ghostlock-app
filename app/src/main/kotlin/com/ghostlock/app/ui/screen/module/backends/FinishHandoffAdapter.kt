package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff

/**
 * UI → 后端（G4L-6）的收尾脚本交接适配器。
 *
 * 职责边界：
 * - **复用**既有收尾契约：`FinishState.toRootScript` 渲染整段脚本、`FinishScriptStore`
 *   写 `files/finish/run.sh`；本适配器不改写脚本内容、不改既有 UI 行为；
 * - 生成后端可接收的不可变 [FinishHandoff]（路径 + 内容副本 + SHA-256），后端据此
 *   对拍「同一个脚本对象」，不解析 terminal 输出；
 * - 不启动攻击、不调用 Native、不做验收判定；验收由后端 `FinishAcceptance` 完成；
 * - 提供**实际执行入口**：脚本由 [FinishExecution] 有界执行，runId 经
 *   `GHOSTLOCK_RUN_ID` 注入、marker 内容与 runId 绑定。
 *
 * 阶段：G4L-6 finish；仅被显式调用，不接默认入口。
 */
internal object FinishHandoffAdapter {
    /**
     * 把当前收尾配置落盘并生成交接对象。
     *
     * @param context 应用上下文（委托给 [FinishScriptStore] 解析 `files/finish/run.sh`）。
     * @param state 当前收尾选项（只读；本函数不修改 UI 状态）。
     * @param managerBinary 已解析的 root 管理器二进制路径；null 表示由脚本自行定位。
     * @param staged slot.id → 已暂存外部路径；与既有收尾路径一致。
     * @param logPath 脚本内 `LOG=` 目标；预览用 `/dev/null`，运行时由执行层传入 memfd 路径。
     * @return 冻结交接对象；渲染/落盘/读回失败返回 null（调用方按 `finish-handoff-invalid`）。
     * @note 脚本是临时物：删除仍走既有 [FinishScriptStore.delete]，本适配器不额外持有文件。
     */
    internal fun build(
        context: Context,
        state: FinishState,
        managerBinary: String? = null,
        staged: Map<String, String> = emptyMap(),
        logPath: String = "/dev/null",
    ): FinishHandoff? {
        val script = runCatching {
            state.toRootScript(managerBinary, staged, logPath)
        }.getOrNull() ?: return null
        val file = runCatching {
            FinishScriptStore.write(context, script)
        }.getOrNull() ?: return null
        val scriptBytes = runCatching { file.readBytes() }.getOrNull() ?: return null
        return FinishHandoff.create(file.absolutePath, scriptBytes)
    }

    /**
     * 有界执行已落盘的收尾脚本，并返回执行证据。
     *
     * @param handoff UI 生成的不可变脚本对象。
     * @param runId 本次运行 id（marker 内容绑定键）。
     * @param logPath 脚本日志路径；null 表示由执行层决定。
     * @return 执行证据（exit/超时/runId/marker 内容）；后端验收只认该证据，不认 terminal 文本。
     */
    internal fun execute(
        handoff: FinishHandoff,
        runId: String,
        logPath: String? = null,
    ): FinishExecutionEvidence = FinishExecution().execute(handoff, runId, logPath)

    /**
     * 构造 run 编排使用的执行 seam（显式传给 `Cve43499Run`）。
     *
     * @param logPath 脚本日志路径；null 表示由执行层决定。
     * @return `(handoff, runId) -> FinishExecutionEvidence` 的有界执行 lambda。
     * @note 该 seam 使后端不必 import ui；不接默认 Flow/UI 入口。
     */
    internal fun executor(
        logPath: String? = null,
    ): (FinishHandoff, String) -> FinishExecutionEvidence =
        { handoff, runId -> FinishExecution().execute(handoff, runId, logPath) }

    /**
     * 删除收尾脚本对象；日志保留（与既有收尾契约一致）。
     *
     * @param context 应用上下文。
     */
    internal fun delete(context: Context) {
        FinishScriptStore.delete(context)
    }
}
