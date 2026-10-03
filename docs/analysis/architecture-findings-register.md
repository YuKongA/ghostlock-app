# 架构发现/约束登记表（Findings Register）

> 用途：**闭包状态**的唯一致口。权威决策在 `adr/0001`–`adr/0004`，执行在 `top-level-architecture-rewrite-plan.md`；
> 审查原文（含被推翻的）在 `architecture-review-log.md`。本表不重复决策正文，只给结论、落点、状态。

| 主题 | 结论 / 约束 | 落点 | 状态 |
|---|---|---|---|
| 组合层接口形状 | `Pipeline<Backend, Terminal>::run(CoreSession&)`；`BackendExecution<B>::run(CoreSession&, terminal::RootedChild&)`；`RunStage::{None,Backend,Terminal}` | ADR-0004 R12；计划“选择与组件模型” | 已定 |
| 去 43499 夹带（primitive/route/victim/service/profile/alias/kernel/support 拆分） | 逐项归属见下 | ADR-0001 §15–§18；计划 Phase A/A2 | 已定，待执行 |
| 依赖模型 | 允许依赖图；backend 不 include pipeline | ADR-0004 R1 | 已定 |
| 轴表述 | 2 装配轴 + backend 内 route + 2 横切 | ADR-0004 R2 | 已定 |
| terminal 轴 / handoff | 顶级 `terminal/`；`handoff` = `terminal::root_child` 内部机制 | ADR-0004 R3 | 已定 |
| ancillary | 机制保持顶级；行为归 `platform::vivo` | ADR-0004 R4 | 已定 |
| platform | `abi`/`runtime`/`vivo`；无 selection id；`applicable(profile)` 门控 | ADR-0004 R5/A2 | 已定 |
| kernel → memory | `kernel` 并入 `memory` | ADR-0004 R6 | 已定 |
| 门禁分批 | A 小批；A2 独立完整门禁；A3/B/C 独立 | ADR-0004 R7 | 已定 |
| 中立性验证 | include 防火墙 + fake backend stub | ADR-0004 R8 | 已定 |
| 同族（UAF）复用 | 明确延后（第二 backend 落地后再抽） | ADR-0004 R9 | 已定 |
| 生命周期 O1–O5 | `PayloadPage` 无隐式析构；`PiRace.request` 借用；victim 转移；终结点顺序；`run_state` 括号 | ADR-0002；Phase 0 计划 O1–O5 表 | 已定 |
| 编译/构建接线 | Makefile 权威（含 host 测试依赖）；CMakeLists 仅 CLion；影集头/harness 同批 | ADR-0001 §19；计划 Phase A/B | 已定 |
| 错误分层 E1–E3 | E1→Fatal/Rejected；E2→非致命；E3→StageResult；异常/间接分派不进 PI 窗口 | ADR-0004 R16 | 已定 |
| profile 注册 A1–A6 | 中性 Document + owner schema；platform/vr 分区；wire 不变 | ADR-0003；ADR-0004 第七轮 | 已定 |
| 控制流 CF1–CF5 | 发现先于写；T0/T1/T2；ancillary 固定点；terminal 由 pipeline；teardown 顺序 | ADR-0004 R13–R17；计划控制流节 | 已定 |
| T1 偏移稳定 / Capabilities 形状 | `Capabilities` 放 backend 状态槽之后（不移动既有偏移）；host-safe/trivially copyable/`noexcept`；`available()` 由句柄推导、不设 flag | ADR-0002；计划“实施约束” | 约束 |
| T2 backend/terminal 类型形状 | backend 类型**不**参数化 route（内部 switch 到编译期实例）；terminal 由 pipeline 选（R12），不在 backend 固定；`run` 签名见上 | ADR-0004 R12；计划“实施约束” | 约束（按 R12 更正） |
| T3 `AddressDiscoveryOps::discover` 签名 | 输出 `{kaslr_base, init_task, target_task, mm_struct}`；`ok=false`/`-1` 即 fail-closed，不保留部分结果；kernelsnitch 与 perf_find_task 同一语义 | 计划“实施约束” | 约束 |
| T4 可测性 | host 替身（Capabilities/platform 行为/terminal 桩）；harness 注入 `CoreSession.capabilities`；测试登记 `NATIVE_HOST_TESTS`（能力契约/平台适用性/终结合同/防火墙/fake backend） | 计划“验证矩阵/实施约束” | 约束 |
| kernelsnitch 许可 | 改写保留许可与署名（vendored） | 计划 Phase A3 | 约束 |

## 说明

- 旧编号（F/O/C/E/A/CF/T）不再作为寻址主键；如需追溯，用主题在本表与 `architecture-review-log.md` 里定位。
- 本表只在**状态变化**时更新（如某项从“约束”变“已执行”），不新增审查叙述。
