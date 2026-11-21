> **历史归档，非当前计划入口** · 原路径：`openspec/changes/archive/2026-11-21-developer-environment/design.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/archive/2026-11-21-developer-environment/design.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Context and scope

沿用[接入设计](../../engineering-workflow/design.md)及其作者自检、独立 critic 阻断0结论，不另开审查。唯一操作文档为[开发环境](../../../../../development/development-environment.md)，固定版本来源保留在原构建与包配置中。

## Decisions

按任务分开文档/规划、Android、host native、Rust 与设备依赖。JDK25 daemon 与 JDK21 compiler 分开；单测真实 builtin plugin 依赖 NDK；旧源码路径/硬编码平台是仓库接线问题，不通过安装更多工具掩盖。

## Verification and authorization

文档链接、版本源及实际 CLI 验证完成后关闭 gl-1w2.1；gl-1w2.2 以真实依赖的拒绝提前关闭、ready 转移、claim、导出重读与原生归档验证证明流程接入。运行证据仅写 Beads；本变更不运行产品构建、AVD 或真机门，这些保持 NOT_RUN。
