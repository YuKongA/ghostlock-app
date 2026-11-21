## Why

E-BUILD-X-001：当前构建闭环修复。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [原精确设计与最新用户优先级](codex://threads/01a1ec39-4b71-7911-a080-05c1b7ae5fa7)。源作者已完成自检与独立critic问题修订，精确实施批准仍待用户确认。迁移不删除.cxx、不执行构建、不改变B1原批准。原聊天的精确设计已原样承接到本 change 的 design.md，便于新 clone 直接阅读；不重写范围，旧提交措辞仅按最新用户纠正同步。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `E-BUILD-X-001/批次01` | 构建闭环修复：确认精确设计与实施批准 | `gl-4on` |
