package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import com.ghostlock.app.backend.cve98260.flow.Flow98260Impl
import com.ghostlock.app.backend.cve98260.flow.Flow98260Request
import com.ghostlock.app.backend.cve98260.payload.Cve98260ExploitPayload
import com.ghostlock.app.backend.cve98260.service.Cve98260Paths
import com.ghostlock.app.backend.cve98260.client.Cve98260Safety
import com.ghostlock.app.backend.cve98260.client.Cve98260ServiceClient
import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.data.cve98260.Cve98260ProfileConfig
import java.io.File
import java.util.UUID

/**
 * UI → 98260 Flow 的请求组装（B3）。
 *
 * 职责边界：只做输入组装与具名校验（profile/payload/Shizuku/boot_id），**不执行、不判终态**；
 * 任一输入缺失/非法 ⇒ [Cve98260RequestBuild.Refused]（fail-closed），不伪造参数。
 *
 * @note 这是薄工厂：真实编排与安全语义在 [Flow98260Impl]（ADR-0022）。
 */
internal sealed interface Cve98260RequestBuild {
    /** 组装成功，可直接交给 `Flow98260Impl().run(request)`。 */
    data class Built(val request: Flow98260Request) : Cve98260RequestBuild

    /** 执行期输入缺失/非法；reasons 稳定可检索。 */
    data class Refused(val reasons: List<String>) : Cve98260RequestBuild
}

internal object Cve98260RequestFactory {
    /**
     * 组装一次 run 请求。
     *
     * @param context 应用上下文（读 assets/nativeLibraryDir）。
     * @param progress 进度回调（UI 注入；默认 no-op）。
     * @return Built/Refused；不抛异常。
     */
    internal fun build(
        context: Context,
        progress: FlowProgress = FlowProgress.NONE,
    ): Cve98260RequestBuild {
        val appContext = context.applicationContext
        val profile = runCatching { Cve98260ProfileConfig.load(appContext) }.getOrElse { problem ->
            return Cve98260RequestBuild.Refused(
                listOf("cve98260-request-factory:profile:" + (problem.message ?: "unknown")),
            )
        }
        val release = runCatching { System.getProperty("os.version") }.getOrNull()
        if (!profile.matchesDevice(release)) {
            return Cve98260RequestBuild.Refused(
                listOf("cve98260-request-factory:release-mismatch:" + (release ?: "unknown")),
            )
        }
        val artifact = Cve98260ExploitPayload.load(appContext)
        if (artifact is Cve98260ExploitPayload.Load.Failed) {
            return Cve98260RequestBuild.Refused(artifact.reasons)
        }
        val loaded = artifact as Cve98260ExploitPayload.Load.Loaded
        val payload = loaded.payload
        val client = Cve98260ServiceClient(appContext)
        val availability = client.availabilityReasons()
        if (availability.isNotEmpty()) {
            return Cve98260RequestBuild.Refused(availability)
        }
        val safety = Cve98260Safety(appContext)
        val bootId = safety.bootId()
        if (bootId.isNullOrBlank()) {
            return Cve98260RequestBuild.Refused(listOf("cve98260-request-factory:boot-id-unknown"))
        }
        val helper = File(appContext.applicationInfo.nativeLibraryDir, HELPER_FILE_NAME)
        if (!helper.isFile || !helper.canExecute()) {
            return Cve98260RequestBuild.Refused(
                listOf("cve98260-request-factory:helper-missing:" + helper.absolutePath),
            )
        }
        val runId = UUID.randomUUID().toString()
        /* finish 与所有 backend 共用同一 UI 生成链路：FinishState.toRootScript →
         * FinishScriptStore（files/finish/run.sh）→ FinishHandoff（path+bytes+sha）。
         * 98260 只消费脚本字节，不生成脚本内容。 */
        val handoff = runCatching {
            FinishHandoffAdapter.build(
                context = appContext,
                state = FinishConfig.state,
                logPath = Cve98260Paths.LOG_PATH,
            )
        }.getOrNull() ?: return Cve98260RequestBuild.Refused(
            listOf("cve98260-request-factory:finish-handoff"),
        )
        val finishScript = handoff.copyScriptBytes()
        /* R1b 回退：service 日志 fd attach 失败时，payload 行由 app 以 SHELL origin 落盘；
         * attach 成功时行已由 service 经 gl_log 写 app fd（NATIVE），不得重复写。 */
        val progressSink = FlowProgress { line ->
            if (!client.isLogAttached()) {
                com.ghostlock.app.log.log.shell(line)
            }
            progress.onLine(line)
        }
        return Cve98260RequestBuild.Built(
            Flow98260Request(
                runId = runId,
                profile = profile,
                payload = payload.bytes,
                payloadSha256 = payload.sha256,
                finishScript = finishScript,
                finishRequiresSelinux = FinishConfig.state.restoreSelinux,
                helperPath = helper.absolutePath,
                port = client,
                safety = safety,
                progress = progressSink,
            ),
        )
    }

    /** 通用 victim helper 文件名（R1e；payload 由 helper execScript 启动）。 */
    private const val HELPER_FILE_NAME = "libghostlock_victim_helper.so"
}
