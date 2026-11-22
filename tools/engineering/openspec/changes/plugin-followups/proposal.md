## Historical status

本 change 保存 `gl-9lz` 等后继任务在 2026-11-21 形成的历史设计与证据，不再表达当前产品 delta，也不是 production 实施入口。原 format 1.1/API 1.1、data-only `PROVIDER`、Diagnose consumer 与 `runhost-wire` exact design 已在 2026-11-22 被 `plugin-architecture` 的 P-BACKEND-X-002/决策04取代；本 change 因此跳过 delta specs。

仍然有效的是产品问题和已经取得的可复用证据：上游只更新设备 profile 时不应要求重装 APK；用户导入数据要保留 provenance、完整性、恢复和 restart/cutoff 语义。这些要求必须在决策04的 backend-bound provider、Document/Artifact 和统一 catalog 模型中重新作 exact design，不能继续实现这里的旧接口。

## Current authority

- 当前插件系统架构：`../plugin-architecture/design.md`。
- 当前 delta specs：`../plugin-architecture/specs/`。
- 任务状态、依赖、批准和实施证据：Beads 中的 `gl-83p`、`gl-9lz` 及对应真实 consumer。
- 本目录 `design.md` 仅供沿革追溯；其当前时态描述应按文件头的 superseded notice 阅读。

## No implementation authorization

本历史 change 不授权 format 1.1/API 1.1、旧 `PROVIDER`、Diagnose Document binding 或任何 product source 改动。决策04获认可后，也只先开放 SDK-v2 exact design gate，不自动恢复本 change 的旧实施范围。
