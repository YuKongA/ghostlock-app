# 架构发现/约束登记表（Findings Register）

> 用途：**闭包状态**的唯一致口。权威决策在 `adr/0001`–`adr/0004`，执行在 `docs/archive/20261007-2237-top-level-architecture-rewrite-plan.md`；
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

| 非内核内存 capability | `FileCacheWriteOps`（与 `KernelMemoryOps` 平级，43503 复用）；43284 无 route/offset | 计划 `docs/archive/20261007-2237-cve-2026-43284-backend-plan.md`；ADR-0004 待补 R | 待拍板 |
| terminal 输入泛化 / `umh_forward` | `TerminalInput`（`RootedChild` 为其中一种）；UMH 不绑 KernelSU | 计划同上；ADR-0004 R10 延续 | 待拍板 |
| per-backend 状态槽构造 | backend policy 提供 `state_type` + `construct/destroy`；组合根按 `selection.backend` | 计划同上；ADR-0002 | 待拍板 |
| CVE-2026-43284 backend | 首个非 43499 backend；独立原语/steps/终态；未真机、未标 supported | 计划 `docs/archive/20261007-2237-cve-2026-43284-backend-plan.md`；评估 `cve-2026-43284-backend-assessment.md` | 计划 |

| Steps 可见（隶属 backend） | `Backend<StepSet>`；`StepSetKind` 经 backend 私有 GLK1 section 下发；profile 唯一权威，native 不设默认/不推导，缺失或未知 → Reject；Kotlin 加载时提示补齐 | ADR-0004 R18/R21；`terminal-steps-redesign.md` | 已定 |
| terminal 统一接口 | `TerminalExecution<T>` 用 `T::Input&`（派生自 `TerminalInput`）；所有 terminal 支持 `RootProgram` 并启动；无虚表（概念 + 静态 policy） | ADR-0004 R19；同上 | 已定 |
| ActivationContext / 非法即拒 | terminal 声明 `Descendant`/`KernelSpawned`；`combination_supported(backend, steps, terminal)` 校验三元组，不自洽直接 `Rejected` | ADR-0004 R20；同上 | 已定 |
| cmp_disasm 定位 | 攻击路径改动必跑并记录差异理由；默认求稳定，允许有理由的机器码变化；`attack_write` 建议置非模板基类以稳定 `do_one_write` | ADR-0004 第九轮；同上 | 已定 |

| profile 二元项 / 字符串 | HOCON 二元项写 `true`/`false`；`backend.steps` 用字符串（`w1_w2`/`w1_w3`），加载时 token→enum→wire；native 标志字段改 `bool`（保布局） | ADR-0004 R18；`docs/archive/20261007-2237-branch-plan.md` T3c；只读调研清单 | T3c 执行中 |

| BackendExecution 泛化 | `BackendExecution<B, Input>`（`Input` 派生 `TerminalInput`）；43499 保持 `run(RootedChild&)`，43284 走 `UmhForwardInput` | ADR-0004 R10；`terminal-steps-redesign.md` | 已执行（pre/post 严格 IDENTICAL） |

## 说明

- 旧编号（F/O/C/E/A/CF/T）不再作为寻址主键；如需追溯，用主题在本表与 `architecture-review-log.md` 里定位。
- 本表只在**状态变化**时更新（如某项从“约束”变“已执行”），不新增审查叙述。
