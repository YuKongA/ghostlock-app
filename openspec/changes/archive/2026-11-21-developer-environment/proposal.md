## Why

新 clone 的 Agent 需要按任务配置真实环境，避免把旧构建接线问题误报为缺少工具。本变更承接现有环境文档整理，作为工具接入的首个真实流程试点。

## What Changes

开发环境指南、双语 README 与 AGENTS 入口；不改生产代码、构建脚本、产品计划内容或批准。不要求产品构建/AVD/真机先通过。

## Capabilities

无产品行为变化，skip_specs。工程规则不在此复制。

## Impact

语义代号 E-DOC-X-002/批次01。本变更交付任务为 `gl-1w2.1`（文档与入口）；用 `npm run --silent bd -- show gl-1w2.1 --json` 读取实际进度和证据。父变更 engineering-workflow 的 `gl-1w2.2` 以本交付为输入，另验证工具集成与归档行为，不属于本变更的产品/文档验收门。
