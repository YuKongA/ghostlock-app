# Lua 批次切页（7 页 · 按依赖链，实测定量）

> 2026-10-09 · Lead · 状态：待认可 · 体量为旧树行数，落点与存在性为新树实测
>
> 本件是**待批事项 4**（见 `docs/analysis/pending-rulings.md`）。C1 的命令级清单在 §7。

## 1 为什么它是七页

目标最后一段是「全力实现 Lua 配置」。实测表明它不是接一根线：`plan_gate.hpp` 的位置是「在 `state_from` 之后、`Backend::run` 之前」，而那两个名字在新树里**只出现在注释里**。继续往下量，又发现 `pipeline.hpp` 依赖的三件也**全树零命中**（`stage_result` · `core_session` · `rooted_child`，**不是改名，是不存在**）。

所以真实结构是**三条分支 + 一次接**，共七页。

## 2 三条分支（可并行，写范围互不重叠）

### 分支 A：会话

| 页 | 件 | 体量 | 前置 | 一句验收 | 证伪点 |
| --- | --- | --- | --- | --- | --- |
| A1 | `session/core_session.hpp` | 48 行 | `contract/capabilities.hpp` · `session/runtime_config.h` | 不透明 backend 状态槽的析构在每条退出路径上都发生一次 | 去掉一处析构调用 ⇒ 断言必须红 |

### 分支 B：终端

| 页 | 件 | 体量 | 前置 | 一句验收 | 证伪点 |
| --- | --- | --- | --- | --- | --- |
| B1 | `terminal/rooted_child.hpp` | 60 行 | `support/native_resource.hpp` · `terminal/terminal_input.hpp` · POSIX | 子进程在成功与失败两条路径上都被回收 | 让回收路径跳过一次 ⇒ 残留会被断言抓到 |

### 分支 C：编排

| 页 | 件 | 体量 | 前置 | 一句验收 | 证伪点 |
| --- | --- | --- | --- | --- | --- |
| C1 | `contract/stage_result.hpp` | 14 行 | **无**（零 include） | 枚举取值与既有阶段名一一对应 | 增一个取值 ⇒ 对应断言必须不平 |
| C2 | `pipeline/component_catalog.hpp` | 218 行 | C1 | 未接线的 token 分派被拒且具名 | 让 switch 漏一个值 ⇒ 编译不过或断言红 |
| C3 | `pipeline/orchestrator.hpp` | 99 行 | C2 | switch 恰好枚举权威能返回的值 | 同上，且须由权威表驱动 |
| C4 | `pipeline/pipeline.hpp` | 96 行 | C1 · C2 · B1 · A1 | 守卫在每条退出路径上都销毁（含失败与提前返回） | 去掉守卫的一处销毁 ⇒ 断言必须红 |

## 3 一次接（依赖全部分支）

| 页 | 内容 | 体量 | 前置 | 一句验收 | 证伪点 |
| --- | --- | --- | --- | --- | --- |
| D1 | `CallSink` → `StagedPlan::record` → 组出 `PlanOp[]` → 门禁 | 小 | A1 · B1 · C1–C4 | 转发一次调用后 staged 项与面行一一对应；未知项由门禁具名拒绝 | 把 surface_index 改错 ⇒ 门禁必须报 UnknownOp 或顺序错 |

## 4 依赖链与并行度

```
C1（零前置）⇒ C2 ⇒ C3 ⇒ C4
A1 ----------------------┘      C4 需要 A1 与 B1
B1 ----------------------┘
C4 + A1 + B1 ⇒ D1
```

三条分支**互不重叠**（`session/` · `terminal/` · `pipeline/` 与 `contract/`），可分别派给不同成员；**C1 零前置**，是整批起点。

与**真机探针**（`docs/analysis/device-probe-43284-plan.md`）**零依赖**：探针走最小组合根，不经门禁 ⇒ 两项可同时推进。

## 5 明确不做的事

- 不把七页塞进一次改动（一个批次只做一类事）；
- 不在 C2 里顺手改 `DispatchTarget` 的取值集合（那是权威表）；
- 不为 D1 临时放宽任何守卫（缺项仍按门禁的具名理由拒绝）。

## 6 判断变更：它为什么是里程碑而不是一个批次（实测轨迹）

这份稿子的页数在测量过程中改过三次，轨迹如下。记下来是为了让后来者不必重走：

| 轮次 | 当时的判断 | 下一次测量推翻了什么 |
| --- | --- | --- |
| 初判 | 4 页（编排两页 + state_from + 接） | pipeline.hpp 的六个 include 里有四个在新树没有家 |
| 二判 | 7 页（补 stage_result · core_session · rooted_child） | 这三件的前置也不存在：capabilities 搬进了 backend，runtime_config / native_resource / terminal_input 全树零命中 |
| 三判 | 10 页，且未到底 | 只有 stage_result（14 行、零 include）是真叶子 |

结论：在新树当下的状态里，这一段的依赖链穿过编排层、终端层、会话层与契约层，长度未知。所以它应当按里程碑推进，而不是按一个批次推进。切页本身仍然有效，它每一步都在减少未知，只是它给出的答案是大，而不是小。

与之相对，真机探针（device-probe-43284-plan.md）走最小组合根，不经门禁与编排，因此不受这条链影响，可以先行。

## 7 C1 的命令级清单（14 行 · 零前置 · 整批起点）

**新件**：`core/src/contract/stage_result.hpp`。内容与旧件逐字对应：`enum class StageResult { Failed, Continue, Done };`，命名空间 `ghostlock::contract`。

**注释必须含三件**（旧件写了其中两件）：

1. 退出码映射：`Failed` 对应 1，`Continue` 进下一阶段，`Done` 对应 0（诊断）；
2. 层级说明：这是**编排阶段的结局**，与 `SurfacePhase`（脚本调用的阶段：`Stage` / `PreWindow` / `Window` / `Terminal`）**不是一回事**。名字相近而层级不同，最容易被后来者搞混，所以写在文件里；
3. 边界：本件零 include，不依赖任何其它件。

**测试**（`core/src/tests/stage_result_test.cpp`）：三条判据 —— 取值恰为三个且顺序一致；三个取值互不相等；每个取值都能被 `switch` 全覆盖（即新增取值会让消费端编译不过或断言红）。

**四处接线锚点**：与既有件同法（文件清单 · `EVIDENCE_BINS` 整行做锚 · rule 2 TAB · check 行 1 TAB），落盘时以当时的行号为准。

**证伪点**：加第四个取值 ⇒ 消费它的 `switch` 不再全覆盖 ⇒ 编译不过或断言红，二者之一必须发生。

## 8 按树重画：到 root 的真实缺口（本文旧数字作废）

本文早先按**旧树**估的页数（7 / 10 / 12 页）与**新树实际不符**：新树比旧树简单，命名也不同。以下按**逐件实测**重画。**旧数字不删，留作沿革**，但以本节为准。

### 已确认存在（实测，非骨架）

- 后端 43499：leak 一族 26 件（碰撞桶/计数/等待、futex 哈希、泄漏搜索/遍历、扫描界/几何、snitch 八件）；memory 有 credential_image 68 行、堆几何/接缝、write_encoder/layout/request；另 perf 三件、race 四件（pi_race 151 行含 RaceOutcome 与 FutexOps 的四个 syscall 包装）、route 六件（policy_dispatch 66 行）、landing 两件、session、steps/step_runner 62 行。
- 终端层：execution 82+56 行（FinishMoment、FinishDeclaration、ExecResult、run_payload）、payload、payload_store。
- 表与门禁：chain_catalog（12 行链）、kind、step_catalog（StepSetOrder 与三个 order）、plan_gate、component_catalog（dispatch_target_of）、attack_window、composition_root。
- 已落地：ADR-0016 的表与判据、C1 StageResult、C2 分派改依 chain_catalog。

### 缺口四项（**全是装配，不是重写攻击**）

## 8 到 root 的真实缺口（2026-10-09 实测修正）

先前那句“全是装配”过于乐观，已按实测更正；**表在 `device-probe-43284-checklist.md`**（执行文档，与清理表同处）。要点：机制与门禁俱在，`memory` 与 `leak` 两族**库在、驱动不在** —— 另外四条同族小段见该表。

### 顺序与判据

1 → 2 → 3 → 4。每项都要：**判据**（含具名拒绝与越界）+ **证伪实验**（造错 ⇒ 以预期方式失败 ⇒ 撤回 ⇒ 核零残留 ⇒ **删被测二进制**）+ 四处接线 + 先 lint 再门禁 + 提交。第 4 项另需**同批登记清理计划**（本仓硬规矩）。

**真机判据（用户口径）**：**成功启动 kernelsu 获取 root**。链路跑通但未拿到 root **不算**达成。
