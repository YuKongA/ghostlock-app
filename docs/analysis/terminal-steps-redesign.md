# Terminal / Steps 重设计（2026-10-03）

> **全局顺序/状态以 `docs/archive/20261007-2237-branch-plan.md`（分支总 plan）为准**；本文件只负责本主题细节。
>
> **⏳ stepset 词汇重命名（用户指令 2026-10-05；实现中）**：本文件的 `W1W2`/`W1W3`（含 `W1W2Steps`/`W1W3Steps`、`Cve43499_W1W2/W1W3`、`DispatchTarget::Cve43499W1W2_*`）现改名为 **`ShizukuRootchild`/`Rootchild`**（词汇 token → `shizuku_rootchild`/`rootchild`）；**数字 wire id 1/2 不变**、`pagecache_write`(3) 不动。**只改 stepset 轴**（`PathKind`/`FrontendKind`/`TerminalKind` 不变）。**两轴正交**：stepset = 跑哪些 W 阶段；frontend/terminal = 谁接管（契约 §3.18）。**本文正文保留设计当时的名**，阅读时按此映射。

> 类型：设计（`docs/analysis/`）。决策落在 ADR-0004 R18–R21。
> 基线：`vr-ko-bypass-dev` / `6ddfcf9`。目的：统一 terminal 接口、让步骤集（StepSet）成为 App 可见的选择项，
> 并解释「为何 root_child 需要 W3 而 UMH 不需要」。

## 1. 问题：W3 到底属于谁

seccomp-bpf 过滤器是**任务属性**：随 fork/clone 继承、execve 保留。zygote 给 app 装了过滤器（禁 `finit_module`），
W2 只把任务变成 uid 0、过滤器仍在 → 需要 W3 清 `TIF_SECCOMP`/`seccomp.mode`。
`call_usermodehelper` 是内核线程起的新 usermode 任务，**不是 app 的后代**，不继承过滤器 → 无需 W3。
故 **W3 需要与否 = (入口是否有过滤器) AND (启动是否在利用任务谱系)**，既非 backend 固有、也非 terminal 固有。

## 2. 概念模型

| 概念 | 归属 | 取值 | 说明 |
|---|---|---|---|
| `EntryContext` | platform/入口（全局单值） | `App`（有 seccomp）/`Shell`（Shizuku、adb，无） | 只用于**校验**，native 不据此推导 Steps |
| `ActivationContext` | terminal | `Descendant` / `KernelSpawned` | 该 terminal 把 root 程序起在谁的谱系 |
| `StepSet` | backend（模板实参、**App 可见**） | 43499：`W1W2`/`W1W3`；43284：`PageCache` | backend 自持步骤词汇 |

## 3. 目标接口

```cpp
namespace ghostlock::terminal {
  struct TerminalInput { RootProgram root_program{}; };
  struct RootedChild     : TerminalInput { /* pid/fd/flags */ };
  struct UmhForwardInput : TerminalInput { /* 通道句柄 */ };
}

concept TerminalExecution<T> = TerminalIdentity<T> && requires(CoreSession& s, T::Input& in) {
    { T::run(s, in) } -> same_as<StageResult>;
};

// backend 由 StepSet 实例化；pipeline 只再传 terminal（R18/R12′）
template <class StepSet> struct Cve2026_43499Backend {
  static constexpr BackendKind kind = ...;
  template <class Route> static StageResult run(CoreSession&, const kernel_offsets&, const char*, bool, Input&);
  template <class M> static Status attack_write(...);   // 建议放非模板基类，保 do_one_write 稳定
};
```

## 4. 组合与选择

```mermaid
flowchart LR
  Sel[profile: backend + steps + terminal] --> Cat{combination_supported?}
  Cat -- no --> Rej[Rejected]
  Cat -- yes --> P[Pipeline<Backend<StepSet>, Terminal>]
  P --> B[Backend<StepSet>::run<Route>]
  B --> T[Terminal::run(CoreSession&, Input&)]
  T --> LP[launch RootProgram]
```

- `ComponentSelection = {BackendKind, StepSetKind, TerminalKind}`；catalog 是**稀疏枚举**的合法三元组；
  `DispatchTarget` 每三元组一项；orchestrator 每项一个 case + `static_assert`。
- **非法三元组直接 `Rejected`**（R20）；native 不设默认、不做 `Auto` 降级。
- 不写任何 `select_steps(...)` 推导；新增条目线性增长，禁止按轴笛卡尔展开（R21）。

## 5. Wire（选项 A）

- Steps 经 **backend 私有 GLK1 section** 下发，例如 `backend.cve_2026_43499` 段的 `steps` 键；**不动 header 布局、不 bump 版本**。
- profile 是 Steps 唯一权威；缺失/未知 → native `Rejected`；**Kotlin 在加载 profile 时提示用户补齐**。
- Kotlin↔native 双侧字段名逐字一致（`profile_binary_test` + Kotlin 协议测试）。

## 6. cmp_disasm 定位

攻击路径改动后**必须**跑 `tools/cmp_disasm.py` 并把差异理由记入门禁记录；默认求稳定，**允许有理由的机器码变化**。
建议 `attack_write`/`zero_word` 放非模板基类 → `do_one_write` 免费稳定；`run_steps`/`w3` 的重组差异按规则复核。

## 7. 触点清单

| 位置 | 文件 | 改动 |
|---|---|---|
| 选择/枚举 | `pipeline/component_catalog.hpp` | `StepSetKind`；`ComponentSelection` 加维度；`combination_supported`/`DispatchTarget`/`dispatch_target_of` 按三元组 |
| terminal 契约 | `pipeline/terminal_contract.hpp` | `TerminalExecution<T>` 用 `T::Input&`；terminal 声明 `ActivationContext` |
| pipeline | `pipeline/pipeline.hpp` | 创建 `Terminal::Input`；`Backend<StepSet>` 实例化 |
| backend | `backend/cve_2026_43499_backend.{hpp,cpp}` | 泛化为 `template<class StepSet>`；`W1W2Steps`/`W1W3Steps`；`attack_write` 移非模板基类 |
| terminal | `terminal/root_child.*`、`terminal/umh_forward.*` | 统一 `Input`/`run`；`RootedChild`/`UmhForwardInput` 启动 `RootProgram` |
| orchestrator | `pipeline/orchestrator.hpp` | 三元组 case；per-backend 状态构造 |
| wire | `profile/binary.{h,cpp}`、backend 私有 section | `steps` 键编解码/校验 |
| Kotlin | `app/.../data/**`、UI | steps 选择 + 加载时提示补齐 + 协议一致性 |
| 测试 | `component_catalog_test`、`backend_contract_test`、`terminal_input_test`、`profile_binary_test`、host dataflow | 三元组/接口/协议 |

## 8. 批次

- **T0**：本设计 + ADR R18–R21 + 登记表/计划（本批）。
- **T1**：`StepSetKind` + `ComponentSelection` 三维 + 稀疏 catalog + `DispatchTarget` + 测试（**不接 backend 实现**）。
- **T2**：`TerminalExecution<T>` 泛化到 `T::Input&` + terminal `ActivationContext` + `umh_forward` 类型转正（执行留 T5）。
- **T3**：wire `steps`（backend 私有 section）+ Kotlin 校验/提示 + 协议测试。
- **T4**：`Cve2026_43499Backend<StepSet>` + `W1W2Steps`/`W1W3Steps` + `attack_write` 非模板基类；保 `do_one_write` 稳定。
- **T5**：`umh_forward` 执行 policy + `RootProgram` 启动 + 组合 + 真机（43284/UMH）。

## 9. 门禁矩阵

| 批次 | host | NDK | lint | cmp | 真机 |
|---|---|---|---|---|---|
| T1 | 三元组/接口测试 | ✓ | ✓ | — | — |
| T2 | terminal 契约 | ✓ | ✓ | — | — |
| T3 | 协议往返 + Kotlin 对拍 | ✓ | ✓ | — | — |
| T4 | 数据流 + 布局 | ✓ | ✓ | 核对 `do_one_write`（应稳定） | 43499 单 route |
| T5 | 组合 + 契约 | ✓ | ✓ | 核对 | 43284 + 43499 各一次 |

## 10. 明确保留 / 风险

- 保留：GLK1 容器中性、profile 唯一权威、43499 的 W1/W2/W3 语义与 PI 生命周期。
- 风险：`TerminalExecution` 泛化触点广（backend/terminal/pipeline/catalog/测试），需分 T1/T2 两批；
  `W1W2` 与 `W1W3` 两份 `run_steps` 实例化增加编译时间与镜像；`RootProgram` 启动路径未真机前不得标 supported。

## 12. T4 逐文件设计（`Backend<StepSet>`）

目标：把 43499 的步骤序列从「单一 `run_steps<Route>`（内含 W1/W2/W3）」拆成**backend 的模板实参**
（`Backend<StepSet>`），让 `W1W2` 与 `W1W3` 成为两个编译期实例；同时保 `do_one_write` 符号与机器码稳定。

### 形状

```cpp
// backend/cve_2026_43499/primitives.hpp（非模板基类：与 StepSet 无关）
struct Cve43499Primitives {
    template <class M> static Status attack_write(CoreSession&, const memory::WriteRequest&, const char*);
    template <class M> static Status zero_word(uintptr_t, const char*);
};

// backend/cve_2026_43499/steps.hpp（步骤 policy，backend 私有的「步骤词汇」）
struct W1W3Steps { template <class M> static StageResult run(CoreSession&, VictimChain&, ...); };
struct W1W2Steps { template <class M> static StageResult run(CoreSession&, VictimChain&, ...); };

// backend/cve_2026_43499_backend.hpp
template <class StepSet> struct Cve2026_43499Backend : Cve43499Primitives {
    static constexpr pipeline::BackendKind kind = pipeline::BackendKind::Cve2026_43499;
    static constexpr pipeline::StepSetKind steps = StepSet::kind;
    template <class Route> static StageResult run(...);   // setup -> StepSet::run<Route> -> 填 Input
};
using Cve43499_W1W3 = Cve2026_43499Backend<W1W3Steps>;
using Cve43499_W1W2 = Cve2026_43499Backend<W1W2Steps>;
```

### 逐文件

| 文件 | 改动 |
|---|---|
| `backend/cve_2026_43499/primitives.hpp/.cpp` | 从现 `cve_2026_43499_backend.cpp` 抽出 `attack_write`/`zero_word`（非模板基类，保 `do_one_write` 符号） |
| `backend/cve_2026_43499/steps.hpp/.cpp` | `W1W3Steps`/`W1W2Steps`：W1→W2（→W3）；`w3` 只在 `W1W3Steps` 实例化 |
| `backend/cve_2026_43499_backend.hpp/.cpp` | `template<class StepSet>`；`run<Route>` 调 `StepSet::run<Route>`；显式实例化 2×3 |
| `pipeline/component_catalog.hpp` | 稀疏三元组加 `{Cve43499, W1W2, RootChild}`；新 `DispatchTarget::Cve43499W1W2_RootChild` |
| `pipeline/orchestrator.hpp` | 新 case：`Pipeline<Cve43499_W1W2, RootChildPolicy>` |
| `tools/cmp_disasm.py` | `do_one_write` TARGETS 保持不变（非模板基类）；`W1W2` 实例无 `w3` |

### 不变量 / 风险

- `do_one_write`（`attack_write<Route>`）符号与机器码**保持不变**（基类非模板）；`cmp_disasm` 应仍 PASS。
- `run_steps`/`w3` 随 StepSet 重组 → 允许机器码变化，记录差异理由（ADR-0004 第九轮定位）。
- 所有权/终结点顺序（O1–O5）不变：`run<Route>` 尾部填 `Input` 的逻辑与今天一致。
- `W1W2` 实例**编译期不含 W3**；`W1W3` 实例行为与今天一致。

### 门禁

host 全绿 + NDK 零告警 + lint 0 + `cmp_disasm`（`do_one_write` 稳定；`run_steps` 差异复核）+ 真机（43499 multicast 冷机）。
## 13. T3d 设计（「一般执行 / Shizuku / UMH」三选 UI）

目标：把 `GhostlockUI.kt` 的 `shizukuEnabled: Boolean` 开关替换为三选，并让它可以驱动 profile 的 `backend.steps`/terminal。

### 语义

| 选项 | 入口 | StepSet | terminal | 备注 |
|---|---|---|---|---|
| 一般执行 | app（zygote，有 seccomp） | `W1W3` | `root_child` | 现状默认 |
| Shizuku | shell（无 seccomp） | `W1W2` | `root_child` | Shizuku 拉起 native |
| UMH | 任意 | `W1W2` | `umh_forward` | T5 落地后可用；当前应置灰 |

### 触点

| 位置 | 文件 | 改动 |
|---|---|---|
| 模型 | `ui/GhostlockUI.kt` 状态 | `shizukuEnabled: Boolean` → `executionMode: ExecutionMode`（enum，UI 层） |
| 动作 | `ui/GhostlockUI.kt` actions | `onShizukuChanged` → `onExecutionModeChanged(ExecutionMode)` |
| 渲染 | `ui/GhostlockUI.kt` | 开关 → 三选控件；UMH 在 catalog 不可用时禁用并提示 |
| ViewModel | `ui/GhostlockViewModel.kt` | 状态与启动路径按 mode 分派（Shizuku→`ShizukuExploitRunner`；一般→app 启动） |
| 资源 | `res/values/strings.xml` + `values-zh` | 三选标签/说明 |
| profile | Kotlin profile 层 | 由 mode 决定 `backend.steps`（一般=w1_w3、Shizuku/UMH=w1_w2）与 terminal（UMH） |

### 决策与约束

- `ExecutionMode` 是 **UI/入口层概念**，不直接进 wire；由它派生 `backend.steps` 与 terminal 选择。
- 与 `recommend_shizuku` 的关系：T3c 移除该项后，**默认 mode** 由 kernel profile 的 `backend.steps` 推导
  （`w1_w3`→一般、`w1_w2`→Shizuku/UMH），不再有独立的 recommend 布尔。
- UMH 选项在 `terminal_available(UmhForward)==true`（T5）之前必须禁用。

### 门禁

Kotlin 单测（mode→steps/terminal 映射 + catalog 禁用）+ Gradle 编译；无 native 改动。
## 14. T5 设计（`umh_forward` 执行 + `RootProgram` 启动）

目标：让 `umh_forward` 从占位变为可用 terminal；它把 App 选的 `RootProgram` 交给**内核发起的 UMH 通道**执行。

### 前置

- 需要一个**后端提供的 UMH 通道**（43284 的 LKM：`call_usermodehelper` / netlink / 设备节点）。43499 没有该通道，
  故 T5 与 **S3 的 43284 backend（B5）** 绑定。
- `TerminalInput.root_program`（已落）携带程序与参数。

### 形状

```cpp
// terminal/umh_forward.{hpp,cpp}
struct UmhForwardPolicy {
    static constexpr pipeline::TerminalKind kind = pipeline::TerminalKind::UmhForward;
    using Input = UmhForwardInput;                 // 已声明
    static constexpr ActivationContext activation = ActivationContext::KernelSpawned;
    [[nodiscard]] static StageResult run(CoreSession&, Input&);
};
// Input 需扩展：UMH 通道句柄/描述符 + lkm_loaded（backend 填）
```

- `run`：校验 `lkm_loaded` 与通道有效 → 把 `RootProgram`（token+argv）写入通道 → 轮询/确认 KernelSU 或所选程序就绪
  （KernelSU 验证仍属 `handoff_probe`，但**不默认绑定**：只有程序是 ksud 时才走 KSU 探测）。
- 缺失通道/`lkm_loaded=false` → `Failed`（fail-closed），不退化为 root_child。

### catalog 与选择

- `terminal_available(TerminalKind::UmhForward)` → true；`combination_supported` 加 `{Cve2026_43284, PageCacheWrite, UmhForward}`；
  `DispatchTarget::Cve43284PageCache_UmhForward`；orchestrator case。
- **不合法组合直接 Rejected**（R20）：如 `{43499, *, UmhForward}`（43499 无 UMH 通道）。

### 门禁

host 契约（`TerminalExecution<UmhForwardPolicy>` 翻真）+ fake backend stub + 真机（43284 路径，另需 `.ko`/设备适配，未过真机不得标 supported）。
## 11. 进度

- [x] T0：设计 + ADR R18–R21
- [x] T1：三维 selection + 稀疏 catalog（真机 `T1` PASS）
- [x] T2：`TerminalExecution<T::Input>` + `ActivationContext`（二进制与 T1 相同）
- [x] T3a：native wire `steps`（backend 私有 section）+ Kotlin `StepSetKind`（真机 `T3` PASS）
- [x] T3b：Kotlin `text`/`bool` 解析访问器 + `backend.steps` 字符串（Gradle 测试 exit 0）
- [x] T3c：移除 `recommend_shizuku`；HOCON 改 T/F；C++ 标志改 `bool`（真机 PASS）
- [x] T3d：「一般 / Shizuku / UMH」三选 UI（`a427471`）
- [x] T4：`Cve2026_43499Backend<StepSet>`（`91723e7`）
- [ ] T5：`umh_forward` 执行 + 真机
