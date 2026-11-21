> **历史归档，非当前计划入口** · 原路径：`openspec/changes/diagnose-stress-design/proposal.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/diagnose-stress-design/proposal.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Why

P-DIAG-NATIVE-001/批次02：真实 consumer 压力设计。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [原需求与最新范围](../../../20261121-1301-backend-root-execution-unification-plan.md#p-diag-native-001-batch-02)。保留producer/churn/>10MiB/drain-handoff需求；旧GLK/JCS及51行scope失效。按真实typed settings/snapshot/cross-process consumer重做设计后仍须原自检、critic及用户批准；不提前实施。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `AS-D1R` | P-DIAG-NATIVE-001：重新细化压力批真实设置消费者 | `gl-cdy` |
