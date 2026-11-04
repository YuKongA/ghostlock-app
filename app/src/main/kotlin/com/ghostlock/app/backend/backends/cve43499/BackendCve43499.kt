package com.ghostlock.app.backend.backends.cve43499

import com.ghostlock.app.backend.declarations.UnfinishedBackend
import com.ghostlock.app.backend.declarations.BackendRow
import com.ghostlock.app.backend.declarations.BackendSpec
import com.ghostlock.app.R
import com.ghostlock.app.backend.declarations.BackendConfigSchema
import com.ghostlock.app.backend.declarations.BackendReadiness
import com.ghostlock.app.backend.declarations.ReadinessReason

/**
 *  CVE-2026-43499 的配置项（multicast 窗口）。
 *
 *  与 43284 分开成文件：两条 backend 的配置项互不影响。
 *
 *  运行就绪声明（task-21）：本 backend 有 Flow run 入口，但**当前不 Ready**：
 *  - 目标环境尚无可证明的运行就绪证据，记为“未验证环境”而不是假 Ready；
 *  - 物理输入（landing.finalBytes、primitive 地址/frame）尚无 Kotlin 计算来源。
 */
@UnfinishedBackend(reason = "original CVE-2026-43499 backend unfinished in exec-glue build")
object BackendCve43499 : BackendSpec {

    /** Keep the original JNI backend visible in the selector but disabled in the exec-glue build. */
    override val selectable: Boolean = false

    override val cve: String = "CVE-2026-43499"
    override val titleRes: Int = R.string.backend_cve_2026_43499

    override fun rows(): List<BackendRow> = Profiles43499.rows()

    /**
     * 当前无可编辑配置（物理输入/地址/frame 由后续批接线后再扩展 schema）。
     *
     * @return 空 schema：不持久化任何配置（fail-closed）。
     */
    override val configSchema: BackendConfigSchema = BackendConfigSchema.EMPTY

    /** backend 提供短文案：就绪 → 「就绪」；当前 → 「未就绪：物理输入未接入」。 */
    override fun statusTextRes(): Int =
        if (readiness().isReady) {
            R.string.backend_readiness_ready
        } else {
            R.string.backend_status_cve43499_not_ready
        }

    override fun readiness(): BackendReadiness {
        /* 真实来源：
         * - 环境：尚无真机门禁结果证明当前目标可运行；
         * - 物理输入：Cve43499RunController 的 physicalInputs seam 默认全 null
         *   （payload/地址/frame 计算批未做），执行器无法组装合法 RunSpec。 */
        return BackendReadiness.NotReady(
            listOf(
                ReadinessReason(R.string.backend_readiness_environment_unverified),
                ReadinessReason(R.string.backend_readiness_cve43499_inputs),
                /* B4 fail-closed：默认 debug 单 W1/perf.enabled=0/无 rooted victim 可能 ⇒
                 * 明确 unsupported-single-w1，不得标 supported（单 W1 路径行为不变）。 */
                ReadinessReason(
                    R.string.backend_readiness_cve43499_inputs,
                    "unsupported-single-w1:default-debug",
                ),
            ),
        )
    }
}
