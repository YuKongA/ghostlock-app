package com.ghostlock.app.backend.cve43499.flow

import com.ghostlock.app.backend.cve43499.finish.FinishExecutionEvidence
import com.ghostlock.app.backend.cve43499.finish.FinishHandoff
import com.ghostlock.app.backend.cve43499.owned.ActorPreparation
import com.ghostlock.app.backend.cve43499.owned.LandingConfig
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig

/**
 * 43499 线性链（G4L-1..G4L-6）的显式输入持有者。
 *
 * 权威：`docs/analysis/43499-flow43499impl-20261026.md`（用户「把 flow43499impl 写好」
 * 的授权记录）。本类只承载已冻结的 Physical/Finish 输入，不含步骤表、不含 route、
 * 不含重试/fallback 策略 —— 顺序与资源终结由 `Cve43499Run` 独占。
 *
 * @property placement Placement 定位事务参数（CPU/超时/扫描窗口/对齐）。
 * @property landing landing 参数与最终页面字节（长度受 Native SKB 闭包约束）。
 * @property actor Actor 调度与 prepare 阶段绑定的 Carrier。
 * @property handoff UI 生成的收尾脚本冻结对象；后端只读、不生成/不解释脚本。
 * @property runId 本次运行 id；marker 内容、执行证据与 victim 绑定都以它为键。
 * @property finishExecutor 可选的收尾脚本有界执行 seam（UI 侧 `FinishExecution` 注入）；
 *   null 表示不提供执行证据 ⇒ 验收会以 `finish-execution-missing` 具名拒绝，不伪成功。
 * @note 构造可见性为 internal：这是模块内部接线对象，不是公开 ABI，也不接默认 UI 入口。
 * @note 阶段：实验实现（flow 接线）；真机未验证，不得据此标 supported。
 */
class Flow43499RunSpec internal constructor(
    internal val placement: PlacementConfig,
    internal val landing: LandingConfig,
    internal val actor: ActorPreparation,
    internal val handoff: FinishHandoff,
    internal val runId: String,
    internal val finishExecutor: ((FinishHandoff, String) -> FinishExecutionEvidence)? = null,
)
