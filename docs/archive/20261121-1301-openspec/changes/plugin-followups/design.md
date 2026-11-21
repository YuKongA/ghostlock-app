> **历史归档，非当前计划入口** · 原路径：`openspec/changes/plugin-followups/design.md` · 日期：2026-11-21 · 原因：有效设计/批准/验收完整迁入 Beads，旧计划/OpenSpec退出执行 · 来源：`f882e670:openspec/changes/plugin-followups/design.md` · 后继：Beads（gl-ind为迁移记录；具体主题从工程代号入口直达）。下方原状态、指令、checkbox及链接仅按当时语境解释，不恢复被取代设计或当前批准。

## Context and scope

P-BACKEND-X-002：按真实 consumer 触发的后续事项。唯一原设计与批准来源：[决策03](../../../20261121-1301-backend-root-execution-unification-plan.md#p-backend-x-002-decision-03)；[BD-I3](../../../20261121-1301-backend-root-execution-unification-plan.md#p-backend-core-001)；[外部extractor归属](../../../../analysis/kotlin-generic-kernel-extractor-plan.md)。原scope、批准、失效关系与门禁仍以该处为准，不在本文件复制。

## Decisions

保留provider、Custom、external import/update、本仓extractor consumer与冻结carrier的原触发条件。不是一个合并实施批；外部库源码/发布不在本仓复制管理。真实backend继续断开，新L级设计批准与目标设备门不能由任务录入替代。

任务细目与旧号映射见[proposal](proposal.md#impact)。当前进度、阻断原因和证据只读对应Beads任务；迁移不是新的设计批准，不用artifact ready覆盖实际判断。

## Verification and authorization

验收方法沿原设计的具名矩阵；未运行门保持NOT_RUN，设计审核不证明实现/真机成功。未来实质设计变化按原作者自检、独立critic与批准条件处理；工具/状态机械维护不重审设计。
