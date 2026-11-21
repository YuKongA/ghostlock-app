## Context and scope

P-DIAG-NATIVE-001/批次02：真实 consumer 压力设计。唯一原设计与批准来源：[原需求与最新范围](../../../docs/plan/backend-root-execution-unification-plan.md#p-diag-native-001-batch-02)。原scope、批准、失效关系与门禁仍以该处为准，不在本文件复制。

## Decisions

保留producer/churn/>10MiB/drain-handoff需求；旧GLK/JCS及51行scope失效。按真实typed settings/snapshot/cross-process consumer重做设计后仍须原自检、critic及用户批准；不提前实施。

任务细目与旧号映射见[proposal](proposal.md#impact)。当前进度、阻断原因和证据只读对应Beads任务；迁移不是新的设计批准，不用artifact ready覆盖实际判断。

## Verification and authorization

验收方法沿原设计的具名矩阵；未运行门保持NOT_RUN，设计审核不证明实现/真机成功。未来实质设计变化按原作者自检、独立critic与批准条件处理；工具/状态机械维护不重审设计。
