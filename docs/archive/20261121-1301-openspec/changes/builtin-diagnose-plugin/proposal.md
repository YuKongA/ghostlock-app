> **历史归档，非当前计划入口** · 原路径：`openspec/changes/builtin-diagnose-plugin/proposal.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/builtin-diagnose-plugin/proposal.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Why

P-BACKEND-X-002/决策03/批次01：builtin Diagnose plugin。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [原批准设计](../../../20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03-batch-01)。2026-11-20生产批准与P0记录保留原§19.5.75；唯一scope是§19.5.70的148行，验收是§19.5.73。旧240行不授权。最新用户先修构建，原批准不撤销；本迁移不替实现者推进production。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `BXP3-B1-I` | P-BACKEND-X-002：BXP3-B1 剩余 source implementation | `gl-dek` |
| `BXP3-B1-V1` | P-BACKEND-X-002：BXP3-B1 普通与包边界验证 | `gl-3ki` |
| `BXP3-B1-V2` | P-BACKEND-X-002：BXP3-B1 AVD 双域与进程死亡验证 | `gl-bvj` |
| `BXP3-B1-V3` | P-BACKEND-X-002：BXP3-B1 ARM64 真机验证 | `gl-xme` |
