## Context and scope

P-BACKEND-X-002：按真实 consumer 触发的后续事项。唯一原设计与批准来源：[决策03](../../../docs/plan/backend-root-execution-unification-plan.md#p-backend-x-002-decision-03)；[BD-I3](../../../docs/plan/backend-root-execution-unification-plan.md#p-backend-core-001)；[外部extractor归属](../../../docs/analysis/kotlin-generic-kernel-extractor-plan.md)。原scope、批准、失效关系与门禁仍以该处为准，不在本文件复制。

## Decisions

保留provider、Custom、external import/update、本仓extractor consumer与冻结carrier的原触发条件。不是一个合并实施批；外部库源码/发布不在本仓复制管理。真实backend继续断开，新L级设计批准与目标设备门不能由任务录入替代。

任务细目与旧号映射见[proposal](proposal.md#impact)。当前进度、阻断原因和证据只读对应Beads任务；迁移不是新的设计批准，不用artifact ready覆盖实际判断。

## Verification and authorization

验收方法沿原设计的具名矩阵；未运行门保持NOT_RUN，设计审核不证明实现/真机成功。未来实质设计变化按原作者自检、独立critic与批准条件处理；工具/状态机械维护不重审设计。
