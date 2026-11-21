> **历史归档，非当前计划入口** · 原路径：`openspec/changes/ui-followups/proposal.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/ui-followups/proposal.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Why

P-UI-CORE-001 与 P-UI-ICON-002：各自后续设计。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [KMP UI](../../../20261121-1301-backend-root-execution-unification-plan.md#p-ui-core-001)；[全量图标需求](../../../20261121-1301-backend-root-execution-unification-plan.md#p-ui-icon-002)。这是两个既有后续需求的导航，不把它们合成一个实施批，也不使Icons成为B1或KMP的前置。各自触发、批准和验收保持。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `P-UI-CORE-001/批次01` | P-UI-CORE-001：按最终 plugin owner 细化 KMP UI | `gl-75t` |
| `IC2-D1` | P-UI-ICON-002：全量图标目录设计 | `gl-72l` |
