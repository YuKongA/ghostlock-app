package com.ghostlock.app.backend.backends.debugprobe

import com.ghostlock.app.BuildConfig
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.core.flow.FlowSymbol
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.BackendUidRequirement
import com.ghostlock.app.backend.declarations.ReadinessReason

/**
 * Debug Probe backend（debug 构建专用；无攻击副作用）。
 *
 * 唯一作用：经新进程 Flow 通道（`:flowhost` / 可选 Shizuku）运行
 * [FlowDebugProbeEntry]，把 host 环境信息写入 Flow log 与 `host.log_path`，
 * 用于验证 log fd/落盘/UI 回调等框架功能。
 *
 * 构建期隔离：
 * - `selectable = BuildConfig.DEBUG`；
 * - `flowSymbol` 仅 debug 非空（release 不进入 registry、不可选、dispatcher 会拒绝）；
 * - release 中 `readiness()` 为 NotReady（具名原因）。
 *
 * @note 仅只读探测；无漏洞/注入/文件系统写（唯一写为本次 run 的日志文件）。
 * @note 阶段：debug probe（2026-11-02 用户要求）。
 */
object BackendDebugProbe : BackendSpec {
    /** 稳定 key（不参与 wire 协议）。 */
    override val key: String = "DEBUG-PROBE"

    /** CVE 身份占位（非真实漏洞）。 */
    override val cve: String = "DEBUG-PROBE"

    override val titleRes: Int = R.string.backend_debug_probe_title
    override val displayNameRes: Int = R.string.backend_debug_probe_name

    /** 仅 debug 构建可选择。 */
    override val selectable: Boolean = BuildConfig.DEBUG

    /** 仅 debug 暴露 Flow 入口；release 为 null ⇒ registry 不含、不可运行。 */
    override val flowSymbol: FlowSymbol? = if (BuildConfig.DEBUG) {
        FlowSymbol("com.ghostlock.app.backend.backends.debugprobe.FlowDebugProbeEntry", "entry")
    } else {
        null
    }

    /** 与执行体无关（App/Shizuku 均可）。 */
    override val uidRequirement: BackendUidRequirement = BackendUidRequirement.ANY_UID

    /** 探测运行可中止（无破坏语义）。 */
    override val abortable: Boolean = true

    /** @return debug=Ready；release=NotReady（debug-only 具名原因）。 */
    override fun readiness(): BackendReadiness = if (BuildConfig.DEBUG) {
        BackendReadiness.Ready()
    } else {
        BackendReadiness.NotReady(
            listOf(ReadinessReason(R.string.backend_debug_probe_disabled)),
        )
    }

    /** @return 静态展示行（host 通道/只读说明）。 */
    override fun rows(): List<BackendRow> = listOf(
        BackendRow(R.string.backend_row_id, cve),
        BackendRow(R.string.backend_row_channel, "flowhost (debug probe, read-only)"),
        BackendRow(R.string.backend_row_payload, "Build/System/proc snapshot"),
    )
}
