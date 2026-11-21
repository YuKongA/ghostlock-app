> **历史归档，非当前计划入口** · 原路径：`openspec/changes/runtime-evidence/proposal.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/runtime-evidence/proposal.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Why

运行/日志未关闭证据与具名技术债。本变更只迁移现有计划入口与执行状态，避免旧勾选项、重复摘要和已取代方向误导执行。

## What Changes

原内容/批准/门禁不变，设计保持在 [原技术债及触发器](../../../20261121-1301-backend-root-execution-unification-plan.md#active-technical-debt)；[Host已完成边界](../../../20261121-1301-backend-root-execution-unification-plan.md#p-run-x-001-batch-03)；[日志原合同](../../../20261121-1301-backend-root-execution-unification-plan.md#p-log-io-001)。按原独立触发保留process-kill、最终树regression、automatic-export真机/最终验收及7项技术债；不把所有欠账接成一条依赖链，不重开已偿还JNA和lifecycle证据。

## Capabilities

本次为tracking-only迁移，没有新的产品合同差异，skip_specs；不复制旧合同到第二套specs。

## Impact

任务状态/运行证据只写Beads；`npm run --silent bd -- show <id> --json`直达，ID不替代原语义代号。

| 原代号/具名项 | 主题 | Beads |
|---|---|---|
| `P-RUN-X-001/process-kill` | P-RUN-X-001：真实 process-kill 证据 | `gl-uat` |
| `AS-V2` | P-DIAG-NATIVE-001：后续改动后的最终树 regression | `gl-32v` |
| `AS-V3` | P-LOG-IO-001：automatic export 真机与最终产品验收 | `gl-f47` |
| `P-PAYLOAD-IO-001/retained-reclaim` | detached Payload retained artifact 回收 | `gl-68z` |
| `P-RUN-IPC-001/protocol-evolution` | GLR1 result protocol 演进 | `gl-51h` |
| `P-DIAG-NATIVE-001/constructor-load` | native-backed Flow 构造期 load 重访 | `gl-v2i` |
| `P-DIAG-NATIVE-001/symbol-authority` | operation/C symbol 单一 authority | `gl-mdl` |
| `P-RUN-CORE-001/held-unknown` | parked/held carrier UNKNOWN 恢复 | `gl-ftu` |
| `P-LOG-IO-001/provider-cancellation` | MediaStore provider 调用不可中断 | `gl-8dj` |
| `P-RUN-IO-001/preview-gc` | preview workspace 生命周期回收 | `gl-dcu` |
