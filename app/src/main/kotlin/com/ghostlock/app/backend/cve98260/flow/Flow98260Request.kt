package com.ghostlock.app.backend.cve98260.flow

import com.ghostlock.app.backend.cve98260.profile.Cve98260Profile
import com.ghostlock.app.backend.cve98260.run.Cve98260SafetyPort
import com.ghostlock.app.backend.cve98260.run.Cve98260ServicePort
import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.core.flow.FlowRequest

/**
 * 一次 CVE-2026-98260 run 的显式输入（B2）。
 *
 * 只承载本次运行的输入与 seam；不放步骤/重试策略（由 [Flow98260Impl] 编译期固定）。
 *
 * @property runId 本次运行 id（稳定、可检索；日志/evidence 绑定）。
 * @property profile 已校验的每构建参数（app 侧解析）。
 * @property payload 静态 payload 字节（来自 app APK nativeLibraryDir）。
 * @property payloadSha256 exploit artifact 的 sha256（evidence；由 RequestFactory 校验）。
 * @property finishScript **项目 UI 生成**的 finish 脚本字节（FinishState→FinishScript；由 rooted victim 执行）。
 * @property finishRequiresSelinux UI 是否要求 finish 后 SELinux=Enforcing。
 * @property helperPath 通用 victim helper 绝对路径（R1e；payload 由 victim 启动）。
 * @property port service 原语端口（生产=Cve98260ServiceClient；测试=fake）。
 * @property safety app 侧安全闸端口（生产=Cve98260Safety；唯一权威=BootSafetyStore）。
 * @property progress 进度回调（默认 no-op；不参与控制流）。
 * @property pollIntervalMs 日志轮询间隔（>0）。
 * @property pollWindowMs 轮询窗口（>0；到期=indeterminate，**不发信号**）。
 * @property maxLogBytes 单次读块上限（1..64 KiB）。
 */
internal class Flow98260Request(
    val runId: String,
    val profile: Cve98260Profile,
    val payload: ByteArray,
    val payloadSha256: String = "",
    val finishScript: ByteArray = ByteArray(0),
    val finishRequiresSelinux: Boolean = false,
    val helperPath: String = "",
    val port: Cve98260ServicePort,
    val safety: Cve98260SafetyPort,
    val progress: FlowProgress = FlowProgress.NONE,
    val pollIntervalMs: Long = 500L,
    val pollWindowMs: Long = 180_000L,
    val maxLogBytes: Int = 64 * 1024,
) : FlowRequest {
    init {
        require(runId.isNotBlank()) { "cve98260-run-id-missing" }
        require(pollIntervalMs > 0L) { "cve98260-poll-interval" }
        require(pollWindowMs > 0L) { "cve98260-poll-window" }
        require(maxLogBytes in 1..(64 * 1024)) { "cve98260-max-log-bytes" }
    }
}
