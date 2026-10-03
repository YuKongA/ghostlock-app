# ADR-0004：框架收敛（第四轮审查裁决）

- 状态：Accepted（维护者授权直接裁决，2026-10-03）
- 日期：2026-10-03
- 基线：`acc5e7b` + 在飞改动（`B0`）
- 相关：ADR-0001（§1/§7/§14 由本 ADR 修订）、ADR-0002、ADR-0003；
  `docs/analysis/architecture-review-log.md` 大框架审查（第四轮）F1–F9

## 背景（Context）

第四轮大框架审查提出 F1–F9（依赖模型、轴表述、terminal/handoff、ancillary、platform 混杂、
kernel/memory 边界、门禁策略、中立性验证、同族复用）。维护者授权直接裁决；本 ADR 记录裁决与后果。

## 决策（Decision）

### R1 依赖模型 = 允许依赖图（不是层次链）
以**允许边（allowed edges）**为唯一权威，并加 include 防火墙测试。允许：

```
support          ← 所有
profile(容器)     ← 所有（只读 document；不反向依赖任何层）
memory           → support
contract         → support, memory
session          → contract, memory, support
ancillary        → contract, memory, support
platform         → contract, memory, profile, ancillary, support
terminal         → contract, memory, session(窄视图), support
backend          → contract, memory, session, platform, ancillary, terminal, profile, support
pipeline         → contract, memory, session, backend, terminal, profile, support
```

禁止：`contract/memory/session/profile/support` 反依赖 backend/platform/terminal；**backend 不得 include pipeline**
（身份声明在 backend、注册表/pipeline 单向引用；`catalog↔backend` 环由此打断，配测试）；platform 不得依赖 backend。

### R2 结构表述 = 2 装配轴 + backend 内 route + 2 横切（定版，替代“四轴”）
```
装配轴：backend（漏洞）× terminal（提权后接管）
后端内部：route（含 primitive/victim，按 profile 选）
横切：platform（厂商/设备/内核对策，profile 门控）· capabilities（可选能力接口）
```
四者不是同级的“轴”。`backend`/`terminal` 由 selection 决定；`platform` 由 profile 设备/厂商段推导，无 selection id；
`capabilities` 是组合根注入的可选能力。ADR-0001 §1 据此修订。

### R3 terminal 成为顶级轴，handoff 降为策略内机制
顶级 `terminal/`：策略 `root_child` / `umh_forward` / `file_write`(43503) / `panic`(23274)；`handoff`（KernelSU
late-load、root script）是 `terminal::root_child` 的内部机制。`RunStage::Terminal` 与之对齐。ADR-0001 §7 修订。

### R4 ancillary 机制保持顶级，行为由 platform/backend 注册
顶级 `ancillary/` 保留为**中性阶段钩子机制**（Controller/Stage；`PreSpawn`/`PostSpawn`/`PreHandoff`；
`AncillaryContext` 退役，行为取 `contract::Capabilities&`）。**行为**（VrGuard/VrTaskTag）归 `platform::vivo`，
未来 backend 行为各自注册；**注册表由组合点注入**（解决架构审查 C 的机制缺口）。ADR-0001 §14 修订。

### R5 platform 内部分层
`platform::abi`（内核 ABI 偏移 + 设备 phys，数据/schema）/ `platform::runtime`（设备运行时探测：iomem cache、
selinux、seccomp）/ `platform::vivo`（厂商对策）。

### R6 kernel 并入 memory
取消顶级 `kernel`：通用地址词汇（`KernelAddress`/域/`DIRECT_MAP_*`/`P0_*`/struct page）与地址数学同归 `memory`。
`kernel/target.h` 中原有的平台默认偏移/符号按 ADR-0003 归属 `platform::abi`/backend。

### R7 门禁按批推进，A2 独立完整门禁
- Phase A（机械搬迁）分小批，每批 host 全绿 + NDK 零告警；
- **Phase A2（语义收敛）独立完整门禁**：host + NDK 零告警 + `cmp_disasm` + 真机归档——语义收敛确有行为风险，
  不得并入单次最终门禁；
- Phase A3 独立（带测试）；Phase B host + Kotlin 对拍；Phase C 另 PR（同前）。
改回“上一批验证通过再进下一批”（AGENTS）。

### R8 中立性/可扩展性机械验证
- include 防火墙测试：中性头（`contract`/`memory`/`session`/`pipeline`/`profile`/`support`）不得拉入
  `backend`/`platform`/`terminal` 头；
- fake backend stub：一个 State/route 都不同的第二 backend 编进 host 测试，证明可组合（仅测试，不进生产）。

### R9 同族（UAF）复用：明确延后
不预抽共享“利用框架接口”。等**第二个原语不同的 backend 真正落地**，再从两个真实实现抽象。此决定写入框架文档，
避免被读成遗漏。

### R10 terminal 输入 = 中性 `terminal::RootedChild`
`root_child` 策略不收 backend 私有 `VictimChain`/`VictimContext`；改用中性、move-only 的
`terminal::RootedChild`（pid + command fd + `alive`/`seccomp_bypassed`/`ever_rooted` + 显式 `retire()`/
`detach()`）。backend 在 terminal 边界由 victim 状态**转移**构造（`release_child()` + fd move），terminal 消费；
作用域退出只关 fd、不杀子进程（保持 O3）。`backend → terminal` 已是允许边。

### R11 backend 内同名子模块加 `backend_` 前缀
`backend::cve_2026_43499::profile` → `backend_profile`、`...::terminal` → `backend_terminal`，与顶级 `profile`/`terminal`
区分；其余 backend 子模块无同名，不改。

### R12 terminal 由 pipeline 编排：`Pipeline<Backend, Terminal>`
terminal 是显式选择维度，由 pipeline 在编排层调用，不藏进 backend：
- `ComponentSelection = {BackendKind, TerminalKind}`（`TerminalKind` = GLK1 header 的 `frontend` 字段语义，wire 字节不变）；
- `Pipeline<Backend, Terminal>::run(CoreSession&)`：先 `B::run(session, RootedChild&)`（backend 填终端输入、转移 victim
  所有权），再 `T::run(session, RootedChild&)`；terminal 返回 `Continue` 视为契约违反；
- 允许边新增 `pipeline → terminal`；catalog 按 `(backend, terminal)` 组合枚举，每 backend 声明自己允许的 terminal 集
  （`BackendIdentity` 提供），route 仍在 backend 内；`RunStage::{None, Backend, Terminal}`。

## 第八轮复核：控制流时序（CF1–CF5，2026-10-03）

运行时序定版（与 R12 对齐：terminal 是 pipeline 步骤，不是 backend 内部）：

```
main: decode Document + selection{BackendKind,TerminalKind} -> CoreSession（构造 backend 状态）
pipeline: Pipeline<Backend, Terminal>::run(CoreSession&)
  B::run(CoreSession&, RootedChild&)
    setup: bind views (ADR-0003) + AddressSpace(platform::abi) + RuntimeConfig tuning
    discovery(可选): AddressDiscovery -> KASLR/task   # 必须在任何写之前
    establish capabilities: KernelMemory Tier1(写引导) -> Tier2 通道探测(B: ashmem->binder->loop / C: pipe)
    stages: W1(+ancillary PreSpawn) -> W2(+ancillary PostSpawn) -> W3 -> 产出 RootedChild
  T::run(CoreSession&, RootedChild&)                 # terminal
teardown: 停止参与者 -> disarm -> 回收
```

- **R13（CF1 时序与降级）**：发现先于写；capability 分级 **T0** route write → **T1** `KernelMemory` 引导写 →
  **T2** 任意读/`update_bits`（T2 依赖 T1 + 通道探测）。T2 不可用 → 回退 T1；行为按声明的能力需求跳过
  （非致命）。初始化/构造失败 E1；能力/通道失败 E2（非致命；必需时降为 E3）。
- **R14（CF2 注册/调度）**：platform 行为以**编译期** `AncillaryPolicyList`（组合点注入，无运行期注册表）注册；
  backend 步骤在固定点调 `ancillary::apply(stage, capabilities)`（`PreSpawn`/`PostSpawn`/`PreTerminal`），
  `enabled(profile)` 门控。
- **R15（CF3 终止动作表示）**：terminal 是组件轴——`TerminalKind`（wire `frontend_id`：root_child/umh_forward/
  file_write/panic）+ `TerminalExecution<T>::run(session, RootedChild&)`；`BackendIdentity` 声明支持的 terminal 集。
  **废止“backend 选 terminal”**：selection 决定 terminal；`backend_terminal` 只负责产出 `RootedChild`/移交状态。
- **R16（CF4 失败传播）**：E1 → `FatalError`/`Rejected`；E2 → 非致命 + 降级/跳过，必需能力缺失时 E3；
  E3 或 terminal 失败 → `RunResult{Failed, stage}`；backend `Done` → `DiagnosticStop`；terminal 返回 `Continue` →
  契约违反 → Failed。**任何路径都执行 teardown。**
- **R17（CF5 拆卸顺序）**：参与者停止 → disarm → 回收（payload/sockets/fd/child）→ capabilities（POD 非拥有）→
  backend 状态（`CoreSession` 析构钩子）→ runtime/AddressSpace；**backend 状态不得先于内核引用析构**。
  `RootedChild` 由 backend 转移给 terminal，terminal 成功可 `detach()`（child 存活）、失败 `retire()`；
  作用域退出只关 fd（O1/O4）。

## 后果（Consequences）

正面：
- 顶级命名空间收敛为 `contract / memory / session / pipeline / backend / platform / ancillary / terminal / profile / support`
  （10 个，含并入的 kernel 与保留的 ancillary）；轴与命名一致，依赖可机械检查；
- pipeline↔backend 环被打断；中立性有测试；门禁可归因。

负面/风险：
- 搬迁面更大（`handoff→terminal`、`kernel→memory` 要改 include/命名空间/构建清单；ancillary 机制保持顶级但行为下放 `platform::vivo`）；
- 需新增 include 防火墙测试与 fake backend stub，host 构建时间略增。

## 第五轮复核补充（2026-10-03，裁决后）

对裁决后的结构复核，固化以下 R1–R3 的推论（S1–S8，原文见 `architecture-review-log.md`）：

- **S2** 身份词汇/`BackendIdentity` → `contract`；执行概念 `BackendExecution`/`FrontendExecution` → `pipeline`
  （因引用 `CoreSession`，不能进 contract，否则 `contract→session`）。
- **S3** `CoreSession` 只放中性字段（`RuntimeConfig`/`contract::Selection`/`memory::AddressSpace`/
  `contract::Capabilities` + 不透明 backend 槽）；`Selection` 词汇在 `contract`。
- **S5** 不新增顶级：装配（Document 解码、view 绑定、`AddressSpace`/backend 状态构造）归 `pipeline`。
- **S8** 允许边补 `pipeline → profile`；backend 状态经 `B::state_from(document)` 构造后存入槽；session 不持 Document。
- **S7** 补“catalogued backend ↔ `Pipeline<B>` 实例”测试。
- **S4** terminal 输入 = 中性 `terminal::RootedChild`（R10）；**S6** backend 内同名子模块加 `backend_` 前缀（R11）。

## 第七轮复核：装配/横切收敛（A1–A6，2026-10-03）

术语定版（并入 A1，替代“四轴”）：**2 装配轴（backend × terminal）+ backend 内 route + 2 横切（platform ·
capabilities）**。原评审表用 `handoff` 指 terminal 轴，按 R3 更正为 `terminal`。

- **A1（表述）**：`backend`/`terminal` 是装配轴；`route`（含 primitive/victim）在 backend 内按 profile 选；
  `platform` 是横切对策集（profile 门控）；`capabilities` 是可选横切能力。不再并列为同级“四轴”。
- **A2（platform 作用域）**：**不新增 selection id**；“当前 platform”由 profile 的设备/厂商/内核段决定；每项
  platform 对策提供 `applicable(profile)` 门控（vr 用 `vr_guard_enabled` 即此形）。
- **A3（wire id，按 R12 更正）**：`frontend_id` = `TerminalKind`（**仍决定 selection**），`middleware_id` = route id
  （**仍由 backend 解释**）——两者都不是残留，**不需要 v3**。变化只在 framing：容器只搬运 u16，合法性由 owner
  注册表校验（ADR-0003 R7）。若将来把 route 完全移进 profile document，`middleware_id` 才降为诊断字段。
- **A4（capability 依赖）**：platform 行为必须声明所需 capability（vr 需 `KernelMemory` 写；read-back 需 `read`），
  经 `ancillary` 的 `Capabilities` 句柄注入；缺能力时 fail-closed/惰性（无 R/W 的 backend 如 23274 → vr 不激活）。
- **A5（调用点）**：platform 行为统一经 **`ancillary` 控制器**在 backend 阶段调用（`PreSpawn`/`PostSpawn`/
  `PreTerminal`），由 backend 步骤显式触发；不在 platform 内自调度。R12 下 terminal 阶段由 pipeline 编排，
  `PreTerminal` 钩子由 backend 在移交 terminal 前触发。
- **A6（GLK1 分区）**：按 ADR-0003 的 owner schema 分区——platform 段（abi/phys/vr）与 `backend::<cve>` 段各自注册；
  **不重命名既有 section**（避免 wire 破坏与版本号），新 section 采用 `platform.*` / `backend.<cve>.*` 命名；
  提取器/Kotlin 按 owner 对齐。

## 第九轮复核：Steps 可见化与 terminal 统一接口（R18–R21，2026-10-03）

动因：接入 CVE-2026-43284（页缓存写 + LKM/UMH 终态）时发现，seccomp bypass（W3）既不是 backend 固有、
也不是 terminal 固有，而取决于「root 程序跑在谁的谱系」：**seccomp-bpf 过滤器随 fork/exec 继承**，
app（zygote）谱系的 root 子进程仍带过滤器、必须清（W3）；内核 UMH 起的新 usermode 任务不继承 app 过滤器，
无需 W3。据此裁决如下（取代/补充 R10、R12）。

- **R18（Steps 可见且隶属 backend）**：攻击步骤集 `StepSet` 是 backend 的模板实参（`Backend<StepSet>`），
  **对 App 可见**，作为一个选择项经 **backend 私有 GLK1 section** 下发（选项 A：不动 header 布局、不 bump 版本）。
  **profile 是 Steps 的唯一权威**：native 不设默认、不做 `Auto` 推导；缺失或未知 → **Reject**；
  Kotlin 在加载 profile 时提示用户补齐。不同 backend 自持步骤词汇（43499 为 W1W2/W1W3；43284 为页缓存链，
  不认识 W1/W2/W3）。
- **R19（terminal 统一接口）**：`TerminalExecution<T>` 以 `T::Input&`（`Input` 派生自中性 `terminal::TerminalInput`）
  为签名；每个 terminal 提供 `Input` 类型与 `run(CoreSession&, Input&)`；**所有 terminal 都必须支持 `RootProgram`
  并实际启动所选程序**（ksud / folkpatch / 自定义 + 参数）。不引入虚表：用概念 + 静态 policy（`-fno-rtti`、
  攻击路径禁间接分派）。`RootedChild`/`UmhForwardInput` 均派生 `TerminalInput`。
- **R20（ActivationContext 与非法即拒）**：terminal 声明 `ActivationContext ∈ {Descendant, KernelSpawned}`；
  `combination_supported(backend, steps, terminal)` 校验三元组自洽（如 `W1W3` 配 `KernelSpawned` 无意义、
  `W1W2` 配 app-入 `Descendant` 会失败）；**不自洽直接 `Rejected`**（fail-closed），不自动降级、不在 native 推导。
  seccomp 需要与否由 StepSet 表达，不由运行期 `process_has_seccomp()` 反推（该检查保留为第二道保险）。
- **R21（selection 与 catalog 形状）**：`ComponentSelection = {BackendKind, StepSetKind, TerminalKind}`；
  catalog 是**稀疏枚举**的合法三元组（不是稠密积）；`DispatchTarget` 每个三元组一项；orchestrator 每项一个 case
  + `static_assert`。新增 backend/steps/terminal 只增显式条目，保持线性，禁止按轴笛卡尔展开。
- **R12′（修订 R12）**：backend **不行为依赖** terminal；pipeline 以 `Terminal::Input` 作为 backend 的编译期输入类型，
  并可用 `StepSet` 实例化 backend（`Pipeline<Backend<StepSet>, Terminal>`）。terminal 仍由 pipeline 选择。

### cmp_disasm 定位（2026-10-03）

`tools/cmp_disasm.py` 回到其原始定位：**攻击路径改动后的机器码核对/调试工具**，不是绝对不变约束。
规则：默认仍求稳定；允许**有理由的机器码变化**，但每次攻击路径改动都必须运行并把差异理由记入门禁记录。
工程上建议把 `attack_write`/`zero_word` 放在**非模板基类**，使 `do_one_write` 符号与机器码免费保持稳定；
`run_steps`/`w3` 随 `StepSet` 重组产生的变化按上述规则复核即可。

## 验证

- 结构：include 防火墙测试 + `backend_contract_test`/`component_catalog_test` 适配（R1）；
- 中立：fake backend stub 编译进 host；
- 门禁：按 R7 分批；A2 跑满 host + NDK + cmp_disasm + 真机。

## 开放决策

无（本 ADR 为裁决记录）。实现细节仍可“一边做一边定”，但不得改变 R1–R9 的结构结论。
