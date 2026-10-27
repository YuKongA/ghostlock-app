package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.R
import com.ghostlock.app.ui.Natives
import com.ghostlock.app.ui.backend.BackendConfigSchema
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.ReadinessReason
import com.ghostlock.app.ui.util.rootAvailable

/**
 *  CVE-2026-43499 的配置项（multicast 窗口）。
 *
 *  与 43284 分开成文件：两条 backend 的配置项互不影响。
 *
 *  运行就绪声明（task-21）：本 backend 有 Flow run 入口，但**当前不 Ready**：
 *  - root/KSU 前置只能读到 stub（`Natives.isManager`、`rootAvailable()` 当前恒 true），
 *    记为“未验证环境”而不是假 Ready；
 *  - 物理输入（landing.finalBytes、primitive 地址/frame）尚无 Kotlin 计算来源。
 */
object BackendCve202643499 : BackendSpec {
    override val cve: String = "CVE-2026-43499"
    override val titleRes: Int = R.string.backend_cve_2026_43499

    override fun rows(): List<BackendRow> = Profiles43499.rows()

    override val runCapability: BackendRunCapability =
        BackendRunCapability.FLOW_RUN

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
         * - root/KSU：Natives.isManager 与 rootAvailable() 是 UI 侧现有探测口；
         *   二者当前均为恒 true 的 stub，只能作为“未验证环境”证据；
         * - 物理输入：Cve43499RunController 的 physicalInputs seam 默认全 null
         *   （payload/地址/frame 计算批未做），执行器无法组装合法 RunSpec。 */
        val rootDetail = "isManager=" + Natives.isManager +
            ", rootAvailable=" + rootAvailable()
        return BackendReadiness.NotReady(
            listOf(
                ReadinessReason(R.string.backend_readiness_unverified_env, rootDetail),
                ReadinessReason(R.string.backend_readiness_cve43499_inputs),
            ),
        )
    }
}
