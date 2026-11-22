# GhostLock 工程与设计规范（Engineering & Design Standards）

<a id="e-review-rule-002"></a>

> **代号**：E-REVIEW-RULE-002（工程支撑－工程审查与实施－规则）；本文章节号只在此根内解释。 定义入口见[工程代号入口](documentation-standards.md#work-identifiers)。

> 本文维护工程任务的实施细则；通用纪律、授权与审查触发见 [AGENTS](../../AGENTS.md#agents-reading)，设计理由见 [设计哲学](design-philosophy.md)。效力服从 [AGENTS §1](../../AGENTS.md#e-review-rule-004) 的教材优先层级；必须/应当/可以分别表示 MUST/SHOULD/MAY。
> **现行结构**只引用所属OpenSpec有效设计及Beads原批准（公共语义 `gl-4tt`，当前插件增量 `gl-dek`）与 [执行视图](full-process-uml.md#run-current-views)，不在本规范重定产品架构或批准状态。

<a id="engineering-task-map"></a>

## 按任务直达

**不必通读全文，也不递归遍历链接。** 从当前任务进入相关小节，仅在具体依赖或矛盾需要时扩读；一项细则只在对应位置维护。以下是导航，不是逐项 checklist，也不增加审批或报告。

| 当前任务 | 直接读取 |
|---|---|
| 判定改动级别、范围与验证要求 | [S/M/L](#engineering-change-levels)、[批次/保留项](#engineering-batches)、[适用验证](#engineering-verification-levels)；需求分流见 [审查原则 §0.4](design-review-principles.md#review-scope) |
| 收到纠正、停止或暂缓验证 | [指令执行](#engineering-execution) |
| 调查、取证、等待、报告/交接 | [停止与汇报](#engineering-investigation)；只在涉及产物身份时读 [身份核验](#engineering-identity) |
| 作者交付 / 独立设计审查 | 分别直达 [作者自检](design-review-principles.md#review-author-self-check) / [critic 方法](design-review-principles.md#review-execution)；是否调用 critic 仍按 AGENTS §7 |
| 编辑/删除文件，生成脚本 | [文件操作](#engineering-file-operations)；代码修改另读 [代码修改与注释](#engineering-code-editing) 及本语言：[C++](#engineering-cpp)、[Kotlin](#engineering-kotlin)、[Rust](#engineering-rust) |
| native 依赖、全局状态或冻结组件 | [依赖边界](#engineering-native-boundaries)、[全局状态](#engineering-global-state)、[保留边界](#engineering-preserved) 中受影响项 |
| 配置、跨端字段、数据类型 | [配置](#engineering-configuration)、[双侧合同](#engineering-cross-side)、[native 数据流](#engineering-native-data)、[类型与例外](#engineering-types) 中受影响项 |
| 资源生命周期、攻击控制流或失败语义 | [资源审查](#engineering-resource-review)、[阶段机](#engineering-attack-stages)、[PI 窗口](#engineering-pi-window)、[错误分层](#engineering-errors) 中受影响项 |
| UI 状态 / 日志 | 分别读 [状态投影](#engineering-ui-state) / [日志](#engineering-logging) |
| 构建、工具链、共享 build | [构建规则](#engineering-build)、[共享构建排他](#engineering-build-writers) |
| 执行测试 / 真机验证 / 实验 | 分别读 [命令](#engineering-gate-commands) + [证据](#engineering-evidence) / [真机门禁](#engineering-device-gate) + [归因](#engineering-causality) / [实验](#engineering-experiments)；适用门由 §1.3 确定 |
| 委派/整合 / 批次提交 | 分别读 [协作](#engineering-delegation) / [提交](#engineering-commits) |
| 文档、图、编号或归档 | [文档规范相关入口](documentation-standards.md#documentation-task-map) |

**历史只供追溯**：§2.0–§2.2、§3.1 旧数据流、§5.1 旧 wire 迁移、§6.1 旧编译行及末尾增补均明确标历史；它们不授权恢复旧实现或新增门禁。2026-10-22 的“Kotlin + JNI”仅是当时快照，出处见 [历史执行架构](../archive/20261104-2030-execution-architecture.md) / [历史 JNI 计划](../archive/20261104-2030-43499-43284-jni-rewrite-plan.md)。

---

## 1. 开发流程：先设计，后改动（强制）

<a id="engineering-execution"></a>

### 1.1 四阶段流程

执行顺序按 [AGENTS §2](../../AGENTS.md#agents-change)：Explore → Design → Implement → Verify。只调查相关代码/条款；需要历史依据时用 `git log --all -- <path>` 定向查证。设计批准与实现验收分开，分级、范围和适用验证分别由 §1.2–§1.4 定义，不要求先实现来取得设计批准。

**收到新指令时，只处理受影响范围**：

| 情形 | 必须执行 | 保留的边界 |
|---|---|---|
| 否定方案、改 owner、暂停或紧急插入 | 用一句话说明“取代什么、保留什么、有效范围/结束条件”；停止受影响动作及委派，同步直接消费的权威条款、当前批次入口和委派 | 只留聊天/日志而旧入口仍生效不算完成；不全库追改历史。含义明确即执行，仅询问影响下一动作的歧义；新证据不自动撤销裁定 |
| 本轮暂缓验证 | 在原验证/进度位置保留 NOT_RUN、门禁欠账、owner 和原恢复触发 | 有明确触发照原文；缺失且妨碍下一步时只问这一点，不自设豁免或把 NOT_RUN 改为 PASS |
| “先修 X/先结论” | 只交付所述内容 | 不自动开放后续批次 |
| “够了/停止” | 结束对应探索，不补最后一次检查 | 不自行重启停止项 |

硬门越权仍按 [AGENTS §1](../../AGENTS.md#e-review-rule-004)，一般催促不是强制确认。

<a id="engineering-investigation"></a>

**调查与汇报**（不另填表；2026-11-03 用户裁定）：

- **决定与停止**：首次扫描/实验前，用一句话或原判据说明要决定什么、哪些结果会改变下一步、何时停止；所有结果都不影响动作则省去检查。证据足够即交付当前唯一成果或关闭具名未知，不把无关代码、测试、manifest 或检查次数当进展。
- **复用与例外**：输入、文件、外部状态和待判假设均未变时，不重复扫描、轮询、评审或汇报；不以换参数/环境包装无信息结果。概率/时序验证设计要求的同构建重复试验保留；连续失败仍按 AGENTS 停批。SHA 只按 [身份规则](#engineering-identity)，不为普通文档反复计算。
- **失败后的共同前提**：连续修复仍在同一判据失败时，停批后先用一句话指出这些修复共同依赖的假设，再选一个能区分假设成立/不成立的观察（如实际入口是否执行、等待对象是否正确、结果是否属于本轮）；根据观察修正前提或继续定位根因，不只换超时、重试次数或增加补偿机制。直接记在原排障位置，不新增表单或审批；原停批条件与门禁不变。
- **报告与交接**：仅在实质结果、失败/依赖变化或用户询问时报告“改动、验证、结果、下一步”，带命令、结果和当前阻断；Lead 将子智能体长输出压缩为决策结论，原始证据仍保留。唯一阻断需用户裁定时，只简短询问一次并停止。
- **不增流程动作**：批准用最短无歧义表达即可；任务只跟踪开发交付项，不跟踪 hash、状态复读或再次评审。状态唯一入口按[OpenSpec / Beads分工](documentation-standards.md#planning-tool-authority)，不继续更新旧计划 checkbox。

<a id="engineering-change-levels"></a>

### 1.2 变更分级与设计产物

| 级别 | 判定 | 设计产物（最小） |
|---|---|---|
| **S** | 单文件内的注释/拼写/纯格式；文档笔误 | 无（直接改） |
| **M** | 普通缺陷修复、非攻击路径的功能/重构、测试补充、文档结构性更新 | 一段改动说明：动机、影响文件、行为差异、验证计划（可写在任务描述或提交信息中） |
| **L** | 攻击关键路径（waiter/race/payload/route/exec 流程）；配置文件格式/wire 协议；跨 Native↔Kotlin 契约；公共数据结构；构建系统；新增 route | **计划文档**（模板见 `documentation-standards.md`）：现状与基线 commit、目标与约束、改动清单（逐文件）、数据流/控制流差异、兼容性与失败恢复（不准备源码 rollback artifact/fixture）、验证矩阵、明确保留项 |

L 级改动**必须**先给出设计并获得用户认可，再写代码。计划模板只在 [文档规范](documentation-standards.md#documentation-templates) 维护；历史范例见附录 B。

<a id="engineering-verification-levels"></a>

### 1.3 验证门槛（按级别）

本表用于实现后的验证，不作为设计阶段必须已有 PASS 的条件。纯文档批验证规则一致性、链接和差异，不运行无关 native/设备门；关键可行性缺证仍按审查原则补证。

| 改动 | 主机测试 | NDK 构建 | lint-tidy | `cmp_disasm` | 真机门禁 |
|---|---|---|---|---|---|
| S/M 非攻击路径实现 | 必须 | 必须零警告 | 必须 0 findings | 可跳过 | 设备依赖行为/端到端结论须真机，否则不需要 |
| M 触及攻击路径 | 必须 | 必须零警告 | 必须 0 findings | 可选诊断 | 必须，按 [§8.3](#engineering-device-gate) 归档 |
| L 实现 | 按改动面，适用门不得省略 | 涉及 native/构建时必须零警告 | 按改动面 | 可选诊断 | 设备依赖行为/攻击路径/端到端结论必须，且归档证据 |

现行命令只在 [§8.1](#engineering-gate-commands) 维护，证据与设备条件见 §8.0/§8.3；AGENTS §8 只保留触发与入口。2026-11-05 沿革：同步 AGENTS 已有 cmp_disasm 可选口径；取代按 L 标签一律索取真机、对纯文档索取构建的旧表述。现有产品门禁与欠账不由本次规则修订自动关闭。

<a id="engineering-batches"></a>

### 1.4 批次化与"明确保留"

- 一批一类事；本批实现通过全部适用门并留结论后才能进入下一批，连续失败停批按 AGENTS §2。
- 计划必须列“明确保留（不做）”；未列入改动清单的代码冻结。现有保留项见 [§2.5](#engineering-preserved) / §5.4，不在各流程重复列清单。

<a id="engineering-commits"></a>

### 1.5 提交与分支

- **本地提交与对外操作分开**：完成已授权批次并满足下述前提后，主动做本地 commit，不等待用户再次要求；用户明确要求暂不提交时遵守该停止条件。push / 建 PR 按 [AGENTS §10](../../AGENTS.md#agents-submit) 另行触发，分支 `very-not-stable-dev` → `main`；不提交 `build/`、`*.so`、`local.properties`。只暂存本批已核对的路径/差异，不使用全仓暂存夹带其他对话、既有暂存或未完成改动；第三方工具的自动提交也必须满足相同范围要求。
- **最终树两个前提**：适用门全绿且检查退出码；被提交文件没有活动 writer（[writer 判定方法](#engineering-writer-state)）。中间态假红不据此提交，须等 writer 停手，或隔离 worktree 按提交重跑。
- **提交表达**：一个提交一个意图，不夹带无关格式化；大改动按批次拆小提交。格式 `type(scope): summary`，type 为 `feat/fix/docs/refactor/test/chore/build/revert`；body 写行为差异、不变量及验证结论。
- **交付与审查**：作者先按 [自检规则](design-review-principles.md#review-author-self-check) 检查原交付；主库变更应由至少一名非作者复核。L 级设计阶段留计划与验证策略，实现交付留适用测试和门禁证据，不混用阶段准入条件；普通代码评审不额外触发 design-critic，其触发仍由 [AGENTS §7](../../AGENTS.md#agents-review) 定义。

---

## 2. 架构规范

<details>
<summary>历史架构快照：§2.0–§2.2（仅追溯时展开，不作为现行判据）</summary>

### 2.0 目标架构（重写进行中；权威 = ADR-0001/0002/**0004**/**0006** + `docs/archive/20261007-2237-top-level-architecture-rewrite-plan.md`；历史目标模型，现行批次见 Beads 当前任务及[语义入口](documentation-standards.md#work-identifiers)）

> **选择权威（2026-10-05）**：装配选择由 **token 白名单**表达（`contract::kCombinationCatalog`：token → {backend, route, steps, terminal, available}），
> token 落在 `backend.<id>.steps`；`pipeline` 只做分派。**词汇/白名单必须导出并对拍**（native → 资源 → Kotlin agreement test），
> 不得在 Kotlin 侧手写第二份。**terminal 归属见 ADR-0006**（词汇保留、实现下放 backend、共享件中性）。
>
> 本节描述重写后的**目标模型**，随重写落地取代 §2.1/§2.2 的现状描述。

**结构（2 装配轴 + backend 内 route + 2 横切）**：
装配轴 `backend`（漏洞）× `terminal`（提权后接管）；`route`（含 primitive/victim）在 backend 内按 profile 选；
横切 `platform`（厂商/设备/内核对策，profile 门控）· `capabilities`（可选能力）。

**命名空间（目标）**：
`ghostlock::{contract, memory, session, pipeline, backend, platform, ancillary, terminal, profile, support}`（`kernel` 并入 `memory`）。

- `contract`：真通用值（`StageResult`、身份词汇 `BackendKind`/`TerminalKind`）+ **可选能力公共父接口**（`RunResult`/`RunCode`/`RunStage` 在 `pipeline`）
  （`CapabilityKind`/`Capability` concept/`Capabilities` + 能力 `KernelMemoryOps`/`AddressDiscoveryOps`）。
- `session`：**组合根** `CoreSession`（框架状态 + `Capabilities` + backend 状态槽；establish 后只读）。
- `backend::<cve>`：漏洞专属（`backend_profile` · primitive · route · victim · service · steps · `backend_terminal` 选择）。
- `platform`：横切对策（`abi` ABI/phys · `runtime` 设备探测 · `vivo` 厂商 vr）。
- `terminal`：跨 backend 终止接管策略（root_child/umh_forward/file_write/panic）；`ancillary`：中性阶段钩子机制
  （行为取 `Capabilities&`，由 platform/backend 注册）；`profile`：GLK1 容器 framing + 中性 Document。
- **判据**：中性=跨所有轴通用；`platform`=跨 backend 绑设备/厂商/内核；`backend`=绑该漏洞。
- **能力模型**：接口（`Capability` 父接口 + `*Ops`）中性、实现在 backend/platform、句柄存
  `CoreSession.capabilities`（组合根）；空句柄 = fail-closed；组件取窄视图
  （backend `CoreSession&`、ancillary `Capabilities&`）。
- 可选发现（`kernelsnitch`/`perf_find_task`）为 `AddressDiscovery` 实现，默认随 backend，复用再提升。

### 2.1 三层与依赖方向

```
app/ (Kotlin, UI/domain/data/shizuku)
  │  JNI（当前生产边界）：POD 参数 + fd/ByteBuffer；无 wire 文档
  ▼
app/src/main/cpp/ (C11 薄原语；无 CLI/无全局)   tools/extract_rs/ (Rust 离线提取器)
  · 历史（2026-10-17 前）：ProcessBuilder 启动 core/src C++23 ELF，stdin 写 GLK1
```

- 当前跨层契约 = **JNI POD + fd/文件 + 进程（child/handoff）**；GLK1/HOCON 仅 legacy 导入与只读参考，
  不是生产 wire。新增跨层状态必须走参数或显式文件，不得发明新环境变量（§3.1）。
- Kotlin 分层：`ui → domain → data`；平台 I/O 只允许在 `data/`；`shizuku/` 是执行入口的变体，
  不承载业务规则。
- Native 命名空间分层：`ghostlock::{route,session,memory,attack,race,support,profile}`（**重写目标见
  §2.0**）；include 一律相对 `core/src`（`#include "route/..."`），让所有权在调用点可见。

### 2.2 route 结构（本项目核心扩展点）

> 现状描述；重写后 `route` 为 **backend 内部策略**（见 §2.0 / ADR-0001）：`Pipeline<Backend, Terminal>`，
> route 由 backend 按 profile 选择；terminal 由 pipeline 编排（本节历史架构的旧 R12 标签，非全局 R12 fail-closed）。

每条 route 由三个部件构成（**可使用虚基类**；但 PI 竞争窗口内的间接调用会引入不可预测时序，见 §4.2）：

1. **Policy**（`route/route_policy.hpp`）：编译期能力声明 + `kind` + `supported()` + `run()`。
   **必须 host 可编译**（主机测试引用每个 Policy）。
2. **Procedure**（已退役：原 `session/exploit_procedure.hpp`；执行策略现由 backend 步骤 + route policy 承担）：
   只覆盖与共享流程不同的步骤；共享步骤（setup/W1/terminal/retry）严禁复制到 route 内。
3. **Route 类**：`prepare → execute → disarm → destroy` 固定生命周期（概念见 `route_lifecycle.hpp`），
   结果只经 `status` 汇报。`execute` 仅在 `prepare` 返回 0 时调用。

本节旧模型的组件触点保留在[历史组件指南](adding-a-component.md#legacy-component-identifiers)；当前改动范围以所属OpenSpec设计与Beads原批准为准，不将旧触点自动带入当前批次。

</details>

<a id="engineering-native-boundaries"></a>

### 2.3 编译边界与条件编译

- Policy、数据结构、纯函数**必须**能在主机（非 Android）编译；真机专属实现放在
  `#if defined(__ANDROID__)` 段内（route 的入口/Procedure/工厂即在其中）。
- 主机测试按 [§8.1](#engineering-gate-commands) 的现行入口；`core/Makefile` / `NATIVE_HOST_TESTS` 是退役记录。测试专用探针不得链接进生产二进制。
- **include 防火墙（历史局部号 R1，非工程规则 R1）**（`include_firewall_test`）：**白名单必须为空**；越层 include 与 stale 条目皆 FAIL；豁免须登记 owner 批次。

<a id="engineering-global-state"></a>

### 2.4 全局状态

- **全局状态**只允许 `g_exploit_session` 与启动期只读 `g_direct_map_end`；不得新增可变全局/引用别名。
- 沿革：§2.0 的 `CoreSession` 目标模型和旧 route 级 static 例外属于历史设计，不另行授权新增全局；现行例外须经原权限确认，不能从历史段落取得豁免。

<a id="engineering-preserved"></a>

### 2.5 上游移植代码与 v1 legacy

- `kernelsnitch/`（含 `futex_hash.h`、`utils.h` 的 `pr_*` 宏）是上游移植代码，**上游已冻结、不再更新
  → 可改写**；但必须在**顶级架构重构拆分之后**作为 backend `leak` 模块进行，并保测试，不得顺手改
  （见 ADR-0001 §17 与 `AGENTS.md`）。
- v1（旧 `offsets.json`）只在 Kotlin 侧由 `LegacyProfileConverter.kt` 转换；native 不再解析 v1。新 route/新字段
  **不得**修改 legacy 转换；兼容输入只做一次性转换。

---

## 3. 数据流规范

<a id="engineering-configuration"></a>

### 3.1 配置数据流：单一权威

- **配置权威按 backend 是否依赖系统内核偏移划分（2026-11-08 用户裁定）**：只有需要系统内核偏移信息的 backend，其配置才以 GLK profile（JSON 源 + JCS wire + **人工维护的 schema**）为权威；不需要系统内核偏移的 backend（包括 43284、Diagnose）以及其它应用设置不进入 GLK profile，各自由具名 typed data class 或专项文件作为唯一权威。执行层不得读配置类环境变量（只允许 `GHOSTLOCK_HOME`/`TMPDIR`/`GHOSTLOCK_KSU_LOG`）；新增内核偏移型 backend 状态时扩展其 GLK profile，其它状态扩展所属具名权威，禁止镜像双写。
- **版本号统一为 3**；唯一迁移点 `LegacyProfileConverter.kt`（v1/v2→3，其余拒绝）；extractor 只认/只产最新版。**静态策略进文档，运行时密钥/SPI/端口绝不进文档**（走会话帧，用后清零）。

<details>
<summary>历史 HOCON/GLK1 数据流（2026-10-22，非当前配置模板）</summary>

**历史数据流（2026-10-22 快照，非当前配置格式或新增 backend 的模板）**：

```
assets HOCON（index.conf + <uname-r>.conf + execution-*.conf + 公共节）
   → Kotlin 解析/合并（include 展开、route 显式选择）
   → 用户覆盖层（按 release 稀疏保存，优先级高于内置与导入）
   → GLK1 二进制 stdin / 后来的 JNI POD 参数
   → native 只读快照
```

</details>

以下为内核偏移型 profile 的匹配与导入约束，不扩展到非 profile 配置：
- `uname -r` 精确匹配；route 由 profile 显式声明，**不得**从 kernel 版本/设备名推断。
- 导入与内置的差异必须可追踪（字段级比较后再提供覆盖），profile 的 release 名与文件名逐字一致。

<a id="engineering-native-data"></a>

### 3.2 Native 数据流

```
TargetProfile（不可变，拥有值）→ resolve_address_space() → ResolvedAddresses（权威快照）
   → WriteRequest（不可变写请求：target/mode/preserve-child）→ Payload 编码（span 界）
   → uintptr_t 写入内核
```

- 可选字段与显式 0 必须区分（presence mask / `optional`）；"0 = 缺省"的隐式兼容只允许存在于
  文档化的一次性推导入口。
- 任何 profile 驱动的缓冲区尺寸/偏移计算必须做溢出安全检查，并在使用前做范围校验（
  `MulticastGeometry`/`Fields` 的既有模式）。

<a id="engineering-resource-review"></a>

### 3.3 资源所有权（RAII 类型）

| 类型 | 用途 | 规则 |
|---|---|---|
| `UniqueFd` | 拥有 fd | 出作用域即关；`BorrowedFd` 表达借用，从不关闭 |
| `MappedRegion` | mmap | 拥有映射；显式 `release_to_process_lifetime()` 才可延寿 |
| `PthreadOwner` | 线程 | join 语义显式；失败置 dirty 而不是丢弃 |
| `ChildProcess` | 子进程 | 所有权显式（`VictimContext`）；作用域退出绝不 signal；退休=SIGKILL+reap |
| `ScopeExit` | 通用清理 | 只在无更具体 owner 时使用 |
| `PayloadPage` | 内核页 | **无析构**（内核引用可超生存期）；生命周期由 HeapContext 显式管理 |
| `std::expected`/`Result<T>` | 可恢复失败 | 失败用 `std::unexpected`；调用点必须处理或上抛 |

- 禁止裸 `new/delete`、裸 `close/join` 的新代码（已登记例外见 §5.4）。
- 资源清理语义（用户态释放 vs 内核悬空 disarm）必须在设计中说清；"dirty" 是合法终态，
  必须写明保留到进程退出还是可回收。

**触及核心攻击资源时的审查（从 AGENTS §5 原位归并）**

范围：waiter/race/payload/route/exec 及其资源的准备/持有/访问/回收。

- **所有权与生命周期**：Rust 式列出每个对象/指针/资源的创建、所有者、借用者、访问区间、终结点、释放者；访问者须先于终结点停止；转移后旧所有者不得再释放或访问。
- **UAF**：逐条查；仅「作为漏洞原语被有意利用的目标」例外，**不得外扩**到辅助对象/race 状态/waiter/缓冲区/映射/同步资源。
- **终结点与顺序**：销毁/回收须在合理边界，覆盖成功/失败/重试/取消/提前返回；**PI 内存与 waiter**：停止访问 → 同步/解除关联 → 回收，先后**不得因重构改变**；缺依据不得合入。
- **记录**：标出核心攻击函数及其资源准备/存活/回收并逐条核对。

---

## 4. 控制流规范

<a id="engineering-attack-stages"></a>

### 4.1 攻击阶段机

```
setup → W1（SELinux）→ W2（凭据）→ W3（seccomp）→ handoff（root child / KernelSU）
```

- 每阶段返回结构化结果，重试统一走 `retry_write_stage()`；**不得**在 route 或阶段里各自实现重试。
- W1/W2/W3 的目标地址与写模式来自不可变 `WriteRequest`；stage 之间不共享可变中间态，
  共享状态只经 `CoreSession` 显式传递；terminal 输入用 `terminal::RootedChild`（ADR-0004 R10）。
- handoff 后的子进程所有权经 `release_child()` 显式转移；探针失败走"退休"而不是盲写。

<a id="engineering-pi-window"></a>

### 4.2 PI 竞争窗口规则

`waiter_thread → owner_thread → consumer_thread → CMP_REQUEUE_PI → route` 窗口内：

- **禁止**异常、锁、日志阻塞调用（用 `log_sync()` 语义的既有路径）；**避免**间接调用（虚函数/`std::function`）——
  **非禁止**，但它会在窗口内增加不可预测时序；若使用，须以真机门禁验证（§8.3）；
- 只允许直接调用与编译期已知的 Policy 能力比较（`route_capability`）；
- 任何"顺手简化"都必须经真机门禁验证（§8.3）；`cmp_disasm.py`（§8.2）是**可选诊断**，用于定位机器码差异范围。

<a id="engineering-errors"></a>

### 4.3 错误处理分层

| 机制 | 使用场景 | 示例 |
|---|---|---|
| `FatalError` | **仅**攻击前致命错误，抛出点自行记录日志，顶层直接退出 | profile 校验失败 |
| `std::expected`/`Result<T>` | 可恢复失败（带 errno 语义） | I/O、解析 |
| `Status`(bool) | 无数据的成败 | 阶段推进 |
| `RouteStatus` | route 结果：OK / fallback-safe / dirty | 只经 `status` 汇报 |
| 能力不可用（`Capabilities` 空句柄） | **fail-closed、非致命** | `KernelMemory`/`AddressDiscovery` 未建立 |

- Native 以 `-fno-exceptions` 语义设计：除 `FatalError` 外不得抛异常；不得用异常表达可恢复错误。
- 失败必须**早失败、带上下文**（profile 校验拒绝发布 active profile；KernelSnitch 越界拒绝）。
- 能力（§2.0）以“空句柄”表达不可用，调用方 fail-closed；**异常与间接分派绝不进 PI 窗口**（E2/E3）。**窗口内可调用面以 [pi-window-constraints](../analysis/pi-window-constraints.md) 为准**（实测：窗口内 `sched_setattr` 进入 `rt_mutex_adjust_pi` ⇒ 禁）。
  > 沿革（2026-10-08）：原句仅禁「异常与间接分派」；现按 device-gate 实证追加「白名单 + 禁项」，原文保留于本行。

<a id="engineering-ui-state"></a>

### 4.4 状态/投影字段语义（UI 与状态设计；从真机缺陷提炼，**强制**）

> **来由**：同一模式在 24 小时内连续造成 3 次真机缺陷（`recommend_shizuku` 升级即崩、`enabledPlugins()` 冷启动抛异常、插件开关「关掉后自己变灰」）。缺陷与修复记录见 `../archive/20261007-2237-branch-plan.md` 的「规则溯源」条目。

| 情形 | 规则 |
|---|---|
| 字段跨用途复用 | 一个投影字段只表达一个语义；可用性/有效性不能直接充当可交互判据，跨用途必须另立并命名。例：`runUsable`＝本次运行可用，`toggleable`＝已安装即可交互，`selected`＝参与本次运行，三者分开 |
| 数据缺失 | 只有缺失本身意味着不可操作时才可禁用；否则保留用户自救入口，将错误显示为诊断行 |
| 构建文档/列表、刷新状态等高频路径失败 | 用 `Ready/Blocked + 原因` 结果化并显示，不以异常中断 UI/进程 |
| 外部/历史数据校验失败 | 容忍并记诊断，不升级为 `require`/异常；只有本版本自己撰写的输入才 fail-closed |
| 缓存填充依赖某页面动作 | 改为初始化或按需解析，避免冷启动依赖路径读空 |

**反例定位**（均已修；完整缺陷与提交沿用上面的 branch-plan 来源，不重复解释规则）：

| 缺陷 | 原错误位置 / 对应性质 |
|---|---|
| `recommend_shizuku` 升级崩溃 | `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:480`，`ProfileLayout.validateLegacy` 对旧数据使用 `require`；对应历史数据容忍 |
| `enabledPlugins()` 冷启动异常 | 文档构建失败抛异常，缓存只在插件页填充；对应失败结果化与缓存填充 |
| 插件“关掉后自己变灰，无法再次开启” | `PluginPresentation.kt:235` 的 `selectable = descriptor != null && errors.isEmpty()` 被 `PluginSettingsUI.kt:174` 当作可交互性；对应 `runUsable/toggleable` 分离与可见诊断 |

---

## 5. 数据结构规范

<a id="engineering-cross-side"></a>

### 5.1 内核 ABI 结构（最高风险区）

当前配置格式/版本见 [§3.1](#engineering-configuration)。

**双侧一致性（现行）**：改一侧同批改另一侧；manifest 清单与生成命令见 [introduction §八](introduction.md)，生成纪律见 §6.5。

- Kotlin↔Native 双侧注册表（`RouteKind`、字段表）由测试锁定（`route_catalog_test` /
  `RouteCatalogAgreementTest`）；键名逐字一致，顺序逐项一致。
- 枚举带显式 wire 值并写入合同注释（`0` 值语义特殊时不得被当普通值使用，如 `kRouteAuto` 不出现在 Kotlin）。

<details>
<summary>历史 MessagePack/GLK1 迁移记录（不构成当前实施要求）</summary>

- **GLKv3（MessagePack）为迁移目标格式**：权威设计见 `docs/analysis/wire-transport-model.md`；native 侧 `profile/glkv3.*`（MPack）、
  Kotlin 侧 `data/profile/Glkv3Encoder.kt`；v2 迁移期只读。
- `profile/binary.cpp` 的 GLK1 wire v2 section/key 表（迁移期）是**契约权威**：字段名、类型与位型一经发布不可改；
  新增字段 = 在该 section 追加一个条目（section/entry 顺序无关），并与 Kotlin `NativeProfile.kt` 同步。
  **重写后分层**：framing（magic/version/组件 id/sections）中性；`sections → TargetProfile` 的绑定归
  **backend**（见 ADR-0001 §12/§17，F6）。

</details>

<a id="engineering-types"></a>

### 5.2 类型设计

- 新类型用 `PascalCase` + `final`；无 invariant 的聚合用 `struct`；有 invariant 的用 `class`。
- 小值类型（句柄、状态）：`noexcept`、`[[nodiscard]]`、可平凡复制时加 `static_assert`（`BorrowedFd` 模式）。
- 视图/容器按用途选型：`std::span`（借用序列）、`std::array`（固定尺寸）、`std::string_view`、
  `std::optional`、`std::expected`；禁止自造可变长 VLA（攻击路径已全部替换为 page-bounded 缓冲）。
- 整数运算：显式宽度（`int32_t`/`uint32_t`…）；混用符号必须显式转换并通过 `-Wsign-conversion`。

### 5.3 不可变性

- 默认 `const`/`constexpr`：编译期能定的不上运行期；只读快照（`TargetProfile`、`ResolvedAddresses`、
  `WriteRequest`）发布后不得再改。
- 需要"可能变更"的数据必须显式建模（mutex/原子量/session 成员），不允许隐式可变全局。

### 5.4 已登记例外（保留，不改）

`fdset_put/get_word` 的 `reinterpret_cast`（POSIX ABI）、`put32/put64`（内核布局写入）、
`iomem_map_span` 的 getline/free、`PayloadPage` 无析构、全局 session 单例、`binary.cpp` 的 `FIELD` 宏。
（`kernelsnitch` 已从“永久例外”移出：上游冻结，可在重构后改写，见 §2.5。）
新增例外必须进计划文档并给出理由。

---

<a id="engineering-code-style"></a>

## 6. 代码风格规范

<a id="engineering-cpp"></a>

### 6.1 C++（`core/src`）

- **禁用 `using`**（别名 · using 声明 · 其它 `using namespace`）；唯一例外 `using namespace ghostlock;`。
- **新代码不得引入任何告警**；编译配置以当前获批构建配置为准。

<details>
<summary>历史编译行（原 AGENTS 已划销，含 RTTI 自相矛盾，不作当前要求）</summary>

- 历史语言记录：C++23、**RTTI 启用**（2026-10-12 用户裁定取消 `-fno-rtti`）、静态 libc++；`-O2 -flto`；`-Wall -Wextra -Wconversion -Wsign-conversion`，
  **新代码不得引入任何告警**。禁止 RTTI/异常/非标准扩展。
</details>

- 头文件：`#ifndef GHOSTLOCK_<PATH>_<FILE>_HPP` guard、自包含、`#include` 相对 `core/src`
  （项目头）、组内按字母序（沿用现有文件的实际风格，不做无关重排）。
- 命名：函数/变量 `snake_case`；类型 `PascalCase`；编译期常量 `kCamelCase`；
  成员变量 `name_`；宏仅限已保留的移植头。
- 格式：4 空格缩进、K&R 大括号、行宽 ~100；`clang-format` 未强制（`FormatStyle: none`），
  但同一文件内保持一致。
- 注释的必要性与格式统一按 [§6.6](#engineering-comment-style)。
- 接口：单参构造与转换运算符必须 `explicit`（有意充当值语义时除外）；不可变性优先；
  编译期检查（`static_assert`/concept）优先于运行期检查。
- clang-tidy：`bugprone-*`、`performance-*`、`clang-analyzer-*`，`--warnings-as-errors='*'`，
  期望 0 findings；category 级排除必须写入 `.clang-tidy` 注释说明理由；单点误报用行内 `NOLINT`。
- 静态/全局对象：只允许平凡析构；动态初始化必须有明确理由（对照 Google Style 的同条规则）。

<a id="engineering-kotlin"></a>

### 6.2 Kotlin（`app/`）

- 遵循官方 Kotlin Coding Conventions（4 空格、trailing comma、表达式体、modifier 顺序、
  命名规则）；项目内额外约定：
  - 默认 `internal`/`private` 收紧可见性；`val` 优先、immutable collection 优先；
  - profile 相关类以 `data class` + `entries()/apply()/from()` 三元组为模式；
  - 与 native 同步的常量/枚举注明对应 native 符号（如 `RouteKind` 的 wire 注释）；
  - 注释的必要性与格式统一按 [§6.6](#engineering-comment-style)。
- 平台 I/O 必须隔离在 `data/`；领域逻辑放 `domain/usecase`；Compose UI 不直接碰进程与文件。

<a id="engineering-rust"></a>

### 6.3 Rust（`tools/extract_rs`）

- `cargo fmt` + `cargo clippy` 作为默认门槛；错误处理用 `Result` + `?`，不 `unwrap` 可失败路径。
- CLI 的格式/版本遵守 §3.1 当前合同；退出码必须有语义，新增 CLI 选项须更新 README 与 profile 适配文档。旧 `--format conf` 默认、`--format json` v1 导入及 preflight `6` 是历史例子，不是当前命令保证。

### 6.4 提交、分支与评审批次

旧入口保留；提交授权、最终树核对与评审职责直接见 [§1.5](#engineering-commits)，不另定批次清单。

<a id="engineering-build"></a>

### 6.5 构建脚本：Gradle-only 与跨平台（用户指令 2026-10-05）

- **构建逻辑一律写进 Gradle KTS**（`*.gradle.kts`）：LKM/DDK、插件产物、native 准备等都由 Gradle 任务承担；**禁止独立 `.sh` 构建脚本**。
- **`.sh` 仅限设备端与运维**：例如 `tools/lkm/ghostlock/root_cmd.sh`（经 `call_usermodehelper` 执行的设备载荷）、`tools/device-guard/*`、`.github/scripts/*`；**第三方/参考项目自己的 `build.sh` 与本规约无关**，文档中照原样引用。
- **跨平台硬要求**：不得依赖 `shasum` / `sha256sum` / `mkdir -p` / `mv` / `cp` / `find` / bash —— 一律用 JVM/Gradle API（`MessageDigest`、`Copy`/`Sync`、`FileTree`）。
- **工具链路径不依赖 PATH**：Android native producer 通过 `androidComponents.sdkComponents.ndkDirectory` 取得与 AGP 一致的 NDK，再从 `toolchains/llvm/prebuilt/*/bin` 定位所需工具；不得硬编码 prebuilt host tag。
- **容器引擎**：探测 `podman` → `docker`，允许 `-PcontainerEngine=` 覆盖；需要容器的任务（如 `buildLkmImages`）必须**显式**执行，**不挂** `preBuild`。
- **跨端列表不得手抄**：8 个 KMI label 由 native 导出 `lkm-kmi-manifest.tsv`（两份逐字节一致）供 Gradle 与 Kotlin 消费——与 `profile-manifest-v3.tsv` / `combination-manifest.tsv` / `vocabulary-manifest.tsv` 同规。

- **产物在仓库内真实 `build/`，禁止任何链接**；出问题 `./gradlew clean`；疑似旧树产物 `realpath` 核实后清理，不得并存两棵树。
- Android module 的 NDK 由 DSL `ndkVersion` 与 SDK side-by-side 安装共同确定，`local.properties` 只写 `sdk.dir`，不用已弃用的 `ndk.dir`；不经 AGP 的独立诊断任务若需要其它路径，必须在其自身入口具名声明。Rust 交叉编译用 rustup 的 `rustc` + `rustup target add aarch64-linux-android`。

<a id="engineering-build-writers"></a>

**共享构建时**：镜像层及仓库 `build/**` 只能有一个 writer；构建前确认客户端=0、容器空闲。共享 build 的 `testDebugUnitTest/buildLkmImages/verifyLkmLedger` 串行执行。并行必须有 `-PlkmLabel=` + 独立 workdir，否则排队；writer 判定只按 [成员状态](#engineering-writer-state)。

<a id="engineering-code-editing"></a>

### 6.6 代码修改与注释

> 2026-11-15 用户裁定：取代 2026-10-07 的强制补注、机械改动标记和注释套旧码要求；原批准、保留项与门禁不变。

**代码修改**：在本批授权范围内直接定点修改，以当前 diff 和适用验证检查差异；不为编辑动作制造注释旧码、TODO 或“机械改动”标记。真实未完成事项才留具名原因和完成条件的 TODO，不以此豁免当前承诺或门禁；实际临时物仍按 [R23](engineering-rules.md#r23) 同批清理。文件保全与写回按 [文件操作](#engineering-file-operations)。

<a id="engineering-comment-style"></a>

**注释判据只在此维护，纳入原作者自检与适用审查，不另设流程：**

- **默认不补叙述**：新增或触及旧码不自动要求补注，不填固定字段，不复述签名或逐行翻译代码；编辑分类与变更经过留在已有交付/决策记录，不写进源码。
- **保留必要信息**：法律/许可、必要公共 API 合同、真实平台/依赖/协议限制，以及代码难以表达且仍有效的非显然约束或关键理由。必要时链接其来源或现有决策，不复制长篇沿革；对是否仍有效有疑问的注释先核实；只有确认重复、失效或不再必要时才删除，存疑不直接删除。
- **解释不代替约束**：对内部变通先核根因，不用长注释为坏模式背书；稳定约束优先由合适的接口、类型或已有检查/测试承接。改行为、合同或 owner 仍走原授权与验证，不为删注释扩范围、增造机制或删除有效检查。
- **只处理本批相关内容**：删除复述、失效解释或不再适用的注释旧码前，确认必要信息已保留；不全库清理，不夹带删除证据、历史记录或明确保留项。注释数量、长度或模板字段不作阻断依据；必要合同缺失按 [阶段与影响](design-review-principles.md#review-stage-blockers) 判断。
- **格式服务内容**：确需接口文档时使用 Doxygen / KDoc / rustdoc，只写有信息价值的内容；局部说明使用该语言常规注释格式，不要求每段代码或内部函数套文档模板。

<a id="engineering-file-operations"></a>

**文件操作**（同样适用于文档）：

- **定点编辑**：锚点必须是完整语句/块，含分号与注释，禁“关键词→右花括号”切法。批量修改先校验全部锚点；脚本执行前自检，任一失败整体不写。脚本生成/替换载荷禁反引号；需要字面值时用转义或临时文件，且不得借此创建下述冗余产物。
- **完整写回**：小修定点编辑；禁止分片读后全文写回。确需全文写回须完整读取，前后核对字节/行数并报告；>50 KB 继续定点编辑。这是编辑方式约束，不是文档篇幅上限。
- **删除或禁用**：死代码判定先查跨进程/第三方调用者（如 Shizuku 的 `destroy()`）；用户要求禁用而非删除时用最可逆形式，恢复清单定位到 file:line。保全与恢复授权仍按 [AGENTS §6](../../AGENTS.md#agents-collaboration)；Git/Time Machine 是用户/owner 的恢复来源，不授权智能体写回。只读可用 `git show <rev>:<path>`、`git diff <rev> -- <path>`、`git log`，不以 `git show REV:path > path` 绕过禁令。
- **禁止冗余产物**（2026-11-03/11-05）：不创建源码/文档副本、验证包（含 `MODIFIED_FILE/DIFF_FILE/VERIFICATION.txt`）、baseline/rollback fixture、回滚脚本或演练；改名、压缩、放在 build/临时目录/仓库外均不例外。普通文档改动不另造验证脚本、重复报告或包装目录：直接改权威文件、看 Git diff，必要命令与结果记入已有审核位置。正常产品构建、已有测试、合同要求的生成 manifest、原始门禁日志不属冗余，不借此删除或跳过。

<a id="engineering-logging"></a>

### 6.7 通用日志

- 级别 V/D/I/W/E，默认 I；V 行级、D 分支、I 里程碑、W 可降级、E 无回退。
- 来源：Kotlin `<>`、native `[]`、shell `()`；fd 行 `[级/毫秒] body`，logcat `file:line body`。
- 一行一次 write、ASCII、禁密钥/令牌；PI 窗口禁日志；失败不影响攻击。
- 原调用点清单见 [日志历史分析](../analysis/log-levels-and-sites.md)；它不另定通用格式或当前 owner。

---

## 7. 文档规范（摘要）

本文不重复文档规则。按任务直达 [双语](documentation-standards.md#documentation-bilingual)、[同批回写/归档](documentation-standards.md#documentation-process)、[计划/门禁模板](documentation-standards.md#documentation-templates)，其它写作、编号与图规则见 [文档任务入口](documentation-standards.md#documentation-task-map)。

---

## 8. 验证与证据规范

<a id="engineering-evidence"></a>

### 8.0 证据分层与计数

- **R14/R15 覆盖边界**：宿主、native 构建、真机各用已有入口与记录索引；不另造报告。纯逻辑按层验证，设备依赖行为、攻击关键路径和端到端成功须 [真机证据](#engineering-device-gate)，缺证不宣称通过。
- **R17 计数**：按本轮声明的测试范围读取实际结构化结果，报告通过/失败（含错误）/跳过，未知或缺少必需 producer 结果不得标 PASS；不以控制台尾部、历史固定总数或报告工具退出 0 代替测试成功。XML 是交换格式，不是门禁本身；完整性清单与执行结果分别核验。
- **R18 测试级别**：在原验证矩阵按改动面选择单元/集成/系统/回归/验收，缺适用覆盖补测；准入范围只由 [§1.3](#engineering-verification-levels) 定义。

**可观察、可重放的行为证据**：复用已有测试、运行入口与原验证矩阵/缺陷记录，说明实际调用入口、输入/初始状态、必要环境或事件/调度顺序、合同预期和实际观察；条件已在具名用例中固定时直接引用，不重复填表。合同来源与用例设计分别按 [审查原则 §3.1](design-review-principles.md#review-contract-boundary) / [D1](design-review-principles.md#review-rule-d1)。

- 从用户/调用者实际接口操作，核返回、错误、可读状态及必要副作用；单独的文件/节点存在、编译绿或报告生成成功，只证明其自身，不替代所声称的行为。UI 反馈核对应皮肤与状态下的实际可见、动作后变化；间距/对齐等视觉要求结合对应截图或可核对布局观测，不强制无关任务录屏。
- 对本批相关、已发生的失败或生命周期问题，优先在原测试中固化输入与事件顺序；可用现有受控调度或故障模拟重放取消/失败窗口，并断言资源终态及后续操作。可重放是复核条件可说明，不要求每种设备现象都确定性复现，也不新增强制变异测试、全局 UI 目录或另一轮验证流程。
- 证据只覆盖实际执行层与条件；未运行保持 `NOT_RUN`，真实失败和跳过照实记录。受控调度、host 或状态机测试不证明真实进程/设备行为，真机准入与判据仍按 [§8.3](#engineering-device-gate)；设计阶段按 [阶段规则](design-review-principles.md#review-stage-blockers) 交验证方法与预期结果，不提前索取实现 PASS。

<a id="engineering-identity"></a>

**身份核验**：SHA 只用于 APK/ELF/profile/golden 等身份敏感产物及本批实际改变的关键文件；不反复给未变化的计划、附件或普通源码算 SHA。普通审核输入标识只按 [G4](design-review-principles.md#review-g4)，不把 HEAD 当作未提交工作树的完整身份。

<a id="engineering-gate-commands"></a>

### 8.1 验证金字塔

**仅执行 §1.3 对本批适用的门；命令在此维护。**

| 对象 | 命令 / 证据 |
|---|---|
| 普通改动的 host 测试 | `./gradlew nativeTestHost --console=plain`：current-tree host CTest 聚合 |
| 普通改动的 Android native 构建 | `./gradlew nativeBuildAndroid --console=plain`：验证唯一 Diagnose `.glplugin.zip` 及 debug/release APK 的 plugin boundary；证明该静态package／APK边界和依赖构建，不证明 RunHost DCL、App／Shizuku进程行为、真机加载或攻击成功 |
| Kotlin 单测 | `./gradlew :app:testDebugUnitTest`；全量结果门沿用 `:app:verifyDebugUnitTestXml` / `:app:verifyReleaseUnitTestXml`（名称兼容，统计与报告由 Allure 负责） |
| Rust 测试 | `cargo test --release` |
| 攻击路径诊断 / 真机 | [§8.2 可选形状诊断](#engineering-diagnostic) / [§8.3 必要设备证据](#engineering-device-gate)，两者不互相替代 |

**报告入口**：上述结果门在原任务内调用 Allure Report 3.33.0 的 `agent inspect`，分别输出 `build/reports/allure/{host-native,android-debug,android-release}/`（`index.md` 给 Agent，`awesome/index.html` 给人，`summary.json` 为官方查询结果）。首用自动安装固定版本 CLI 至 `build/tooling/allure`，需要 Node.js ≥22.18 与 npm；不加测试运行器或另一轮检查。Agent 先读对应 `index.md`，按需用 `build/tooling/allure/node_modules/.bin/allure agent query --from <报告目录> summary` / `tests --status failed` 定点读取，不全量复读 XML。inspect 只解释结果，不证明命令执行成功；Test/CTest 原退出状态与当前 producer 证明仍由 Gradle 持有。Android 全量门保留零失败/错误/跳过；host 允许的 skipped/disabled 明示为跳过，不计通过。debug/release 分开，定点测试不冒充全量；真机判据仍见 §8.3。

主机单测覆盖固定向量、生命周期、几何边界和双侧一致性；新增数据结构/route 必须补适用测试。

**退役与证据限制**：`verifyDebugNativeArtifacts` / `verifyReleaseNativeArtifacts` 已于 2026-11-06 按用户裁定删除，普通构建不等于 ELF/ABI/JNI 精确核验。随 2.0.5 退役的 `core/` 构建树（旧 `check` / `android` / `native-host-tests` 目标及 `NATIVE_HOST_TESTS` 登记）不再作为当前门禁，不恢复旧路径冒充入口。

<a id="engineering-diagnostic"></a>

### 8.2 攻击函数形状对比（可选诊断，**非门槛**）

```sh
python3 tools/cmp_disasm.py <baseline-binary> build/native/ghostlock
```

`TARGETS` 覆盖 8 个攻击函数，可加入新函数；只定位受影响函数、位移/形状差异，不以反汇编差异阻断批次。运行后结论记入原提交信息/计划，攻击路径判据仍是真机门禁。

<a id="engineering-device-gate"></a>

### 8.3 真机门禁（攻击路径改动必须）

- **前置**：冷机、固定 CPU 对、单 route、KernelSU 未加载；记录设备 uptime。
- **判定**：确认 route 命中与写验证；`route_done status=0 clean=1/1`、`child is root!`、`exploit complete`、handoff `KernelSU ready`，无 kernel panic；至少重复运行并记录次数。
- **证据与归档**：取设备 `Download/ghostlock-debug-log/<时间>/*.log.txt`（含 `profile.conf` / `profile.bin`）；按 [既有门禁模板](documentation-standards.md#documentation-templates) 写入 `docs/analysis/device-gates/*.md`，记录与日志对应。失败/panic 与通过同等归档、格式一致。
- **结论边界**：新 profile 未过真机不得标 `supported`；设计审核、host 或构建通过不代替真机通过。

<a id="engineering-causality"></a>

### 8.4 归因纪律

- **`KERNEL-PANIC-01`**：同构建可出现 PASS/panic/PASS；代码因果须同构建复现、冷机复跑及栈证据，单次 panic 不得归因代码。
- **历史证据不外推**：Multicast（5.15）、TCP、Select 的历史记录只证明其对应构建/设备，不证明当前树；未验证路径不宣称支持，profile 的准出条件见 §8.3。
- **提交归属如实写**：未提交只写“已实现（工作树，待提交）”；“已落地”须绑定实际包含改动的提交，提交后回填，不把工作树或其它提交当来源。沿革：曾将“对齐”误记到仍含旧拼写的提交，本条防止同类误归因。

<a id="engineering-experiments"></a>

### 8.5 证伪与重建（仅采用该实验时）

- **入口**：新实验从生产路径启动，禁止加 CLI 旗标/旁路；仅服务实验的入口同批登记清理计划（默认技术债）。
- **覆盖与证伪分开**：2026-10-07 起，强制覆盖按 [审查原则 D1](design-review-principles.md#review-rule-d1)，守卫证伪仅为可选加强。采用时按 [R42](engineering-rules.md#r42) 证明注入落盘且破坏目标性质、目标包含被测件、产物已重建，同时检查 stdout/stderr；未失败不自动证明断言无用。
- **重建**：源码回位不等于产物回位。实际使用 Make 的实验仍用 `make -B`，实验后 `rm -f <artifact>` + `make -B` 复核；当前 Gradle 则按 §8.1，不恢复退役 `core/` 目标或对纯文档要求构建。文件操作与副本禁令按 [§6.6](#engineering-file-operations)，实验不授权恢复工作树或准备 rollback 演练。

---

<a id="engineering-delegation"></a>

<a id="9-agent-执行检查清单先设计后改动"></a>

## 9. 委派与整合

**只在委派、整合或共享工作树时读取；不是新的 checklist 或审批。**

- **委派边界**：可并行、自包含的工作优先委派，可递归拆分；每项给目标、互不重叠的写范围、命令与期望、验收标准。成员只做所委派任务，按任务加载匹配的技能/工具，不遍历全部目录。
- **交付责任**：作者依 [自检规则](design-review-principles.md#review-author-self-check) 修正原交付并返回原始日志、退出码；Lead 核对整合差异和跨模块合同，不逐份重做全量自检，仍承担本人设计与整体验收责任。简报按 §1.1，真机归档按 §8.3；自检不替代 [非作者审查](#engineering-commits)。

<a id="engineering-writer-state"></a>

- **成员与 writer**：用 `spawn_teammate` 建常驻成员，同模块/写范围复用同一人；同一共享工作树只允许一条写入流，未指派者只读。判定有无 writer 看成员状态，不看 mtime；构建排他另按 [§6.5](#engineering-build-writers)。
- **既有分工**：原则/简并＝design-critic，native＝native-core，Kotlin/配置＝kotlin-app，文档/结构图＝docs-uml；冲突以原则与物证裁决。

---

## 附录 A：外部参考（2026-09 调研）

| 来源 | 采用内容 |
|---|---|
| [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) | P.1 直接表达意图、P.4 静态类型安全、P.5 编译期检查优先、P.8 不泄漏资源、P.10 不可变优先、P.11 封装混乱构造、RAII/expected 资源模型 |
| [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) | 为读者优化（调用点留下所有权痕迹）、与既有代码一致、自包含头文件、静态/全局对象的平凡析构约束、`explicit` 与 copy/move 明确、规则必须"值得" |
| [Kotlin Coding Conventions](https://kotlinlang.org/docs/coding-conventions.html) | 命名/格式/类布局、不可变优先、named arguments、KDoc 写法、trailing comma |
| [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/) | 命名与文档、可预测性、健壮错误处理、面向 CLI 的契约稳定 |
| [Conventional Commits 1.0.0](https://www.conventionalcommits.org/) | 提交格式与类型集、breaking change 表达 |
| [ADR](https://adr.github.io/)、[MADR 模板](https://adr.github.io/adr-templates/) | 决策记录要素：背景/决策/理由/备选/后果 |
| [Diátaxis](https://diataxis.fr/) | 文档四象限（tutorial/how-to/reference/explanation）与不混写原则 |
| [AGENTS.md 开放格式](https://agents.md/) | agent 指令文件定位、嵌套/就近优先、命令会被实际执行、作为 living documentation 维护 |
| [Software Engineering at Google（Fergus Henderson, 2017，中文校订版）](https://github.com/laofo/SoftwareEngineeringAtGoogle) | 代码审查（至少一名非作者、小变更、开发中评审）、测试随变更强制、事后剖析字段（影响/时间线/根因/行动项）、受控重写 |
| [Anthropic: Claude Code best practices](https://www.anthropic.com/engineering/claude-code-best-practices) | Explore→Plan→Implement→Commit、可执行验证闭环、证据优于断言、spec 先行、对抗式复核、避免上下文污染 |

## 附录 B：本地出处（均在 git 历史中可检索）

| 主题 | 出处 |
|---|---|
| 解耦设计流程与验收 | `DECOUPLING_PLAN.md` / `DECOUPLING_LOG.md`（commit `432a05f` 等） |
| C++ 迁移批次与门禁 | `docs/analysis/native-cpp-migration-plan.md`、`docs/development/native-modernization-plan.md` |
| 组件扩展清单 | `docs/development/adding-a-component.md`（现行） |
| 全局状态口径 | `docs/analysis/native-global-state.md` |
| 入口/CLI 契约 | `docs/analysis/native-entrypoint-plan.md` |
| 环境变量收敛 | `docs/analysis/environment-convergence-plan.md` |
| 门禁记录样例 | `docs/analysis/device-gates/**`（S04–S15、CPP00–CPP17、U01、NS*、PROFILE-*） |
| 首攻实验史（负结果） | `repro/xperia-first-success/README.md`（V1–V21） |
| 警告/clang-tidy 策略 | `docs/analysis/native-warning-audit.md`、`src/.clang-tidy` 注释 |

## 附录：批次与写入流规则增补（2026-10-05）

> **历史记录，非并行/对外发布授权**：单 writer 和提交纪律仍按 [AGENTS §6、§10](../../AGENTS.md#agents-collaboration)；操作细则分别见 [共享构建](#engineering-build-writers)、[委派](#engineering-delegation)、[提交](#engineering-commits)；跨端导出按 §5.1/§6.5。2026-11-21 用户纠正后，本地 commit 改为完成已验证批次后的默认动作；旧“未经要求不提交”不再生效，push / PR 仍另行触发。下列词汇模型不扩大当前计划范围；历史 lint 红线在对应工具启用时仍按 §6.1 执行。

- **单写入流**：同一时刻只允许一条写入流改共享工作树；并行工作限只读。批次落地必须**原子提交**。
- **导出对拍**：跨语言词汇表（route 目录 / GLKv3 字段 / **组合 token 白名单** / 可用性）只有 native 一份权威，
  必须导出到资源并由 Kotlin 运行时读取 + agreement test；Kotlin 源码中的 token 字面量只允许出现在测试里。
- **lint 红线**：`clang-analyzer-optin.performance.Padding` 等以 `-warnings-as-errors` 生效；新增公共结构体必须一次排好字段序。

> **2026-11-08 整理沿革**：在本文件内合并重复条款，以任务直达表替代长篇阅读说明；纠正/调查、writer、文件操作、命令/证据各归一处。旧章节锚点保留，历史原文只供追溯；作者自检、独立审查、真机门禁、计划内容和批准状态不变，不新增文件或流程。
