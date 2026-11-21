## Why

P-SETTINGS-IO-001/批次01：Android runtime 验证。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [原设计、批准与验证矩阵](../../../docs/plan/backend-root-execution-unification-plan.md#p-settings-io-001)。source/普通门既有证据保留；只承接DS-V1欠账，不改设备授权、Android判据或配置行为。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `DS-V1` | P-SETTINGS-IO-001：DataStore Android exact runtime | `gl-zgp` |
