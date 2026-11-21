> **历史归档，非当前计划入口** · 原路径：`openspec/changes/plugin-followups/proposal.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/plugin-followups/proposal.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Why

P-BACKEND-X-002：按真实 consumer 触发的后续事项。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [决策03](../../../20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03)；[BD-I3](../../../20261121-1301-backend-root-execution-unification-plan.md#p-backend-core-001)；[外部extractor归属](../../../../analysis/kotlin-generic-kernel-extractor-plan.md)。保留provider、Custom、external import/update、本仓extractor consumer与冻结carrier的原触发条件。不是一个合并实施批；外部库源码/发布不在本仓复制管理。真实backend继续断开，新L级设计批准与目标设备门不能由任务录入替代。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `BD-I3` | P-BACKEND-CORE-001：由真实 consumer 触发配置实现 | `gl-2zi` |
| `P-BACKEND-X-002/provider` | P-BACKEND-X-002：data-only profile provider / Document | `gl-9lz` |
| `P-BACKEND-X-002/Custom` | P-BACKEND-X-002：真实 Custom consumer 的公开能力合同 | `gl-tz3` |
| `P-BACKEND-X-002/extractor-consumer` | P-BACKEND-X-002：外部 extractor Maven consumer | `gl-r8h` |
| `P-BACKEND-X-002/import-update` | P-BACKEND-X-002：external import/update 与 READY 后对象保护 | `gl-dt8` |
| `P-BACKEND-NATIVE-001/批次01` | 真实 backend 候选：43284 保持断开 | `gl-2u2` |
| `P-BACKEND-NATIVE-002/批次01` | 真实 backend 候选：98260 保持断开 | `gl-2ot` |
| `P-BACKEND-X-002/43499-consumer` | 真实 backend 候选：43499 保持断开 | `gl-m0f` |
