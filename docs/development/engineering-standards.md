# GhostLock 工程与设计规范（Engineering & Design Standards）

> 思想层（为什么/信什么）见 [`design-philosophy.md`](design-philosophy.md)——11 条原则均以软件工程理论
> （SWEBOK / ISO/IEC/IEEE 12207 / ISO/IEC 25010 / 经典文献 / 工业实践）为依据，本文是做法层（怎么做/门槛）。
> 适用对象：本仓库的人类开发者与 AI agent。**任何改动开始前必须先读本文并完成第 1 节的设计流程。**
> 用语按 RFC 2119：**必须**（MUST）/ **应当**（SHOULD）/ **可以**（MAY）。
> 效力顺序：用户当期要求 > `AGENTS.md` > 本文 > 其他文档。本文与代码不一致时，以代码为准并**顺带修正本文**。

本文由仓库历史文档（`docs/analysis/**`、`DECOUPLING_PLAN.md`、`native-cpp-migration-plan.md`
等，已归档在 git 历史）与外部权威规范（见附录 A）合成，描述的是**这个项目实际执行过并被验证有效**的做法，
不是通用说教。

---

## 1. 开发流程：先设计，后改动（强制）

### 1.1 四阶段流程

```
Explore（只读调查） → Design（设计产物） → Implement（最小实现） → Verify（分级验证）
```

- **Explore**：动手前先读相关代码与文档；用 `git log`/`git show` 查阅历史设计与证据（本仓库的
  `docs/analysis/**` 与 device-gates 在 git 历史中完整保留）。调查范围要窄，不确定就声明假设。
- **Design**：按下表产出一个**可评审的设计**。设计未完成不得进入实现。
- **Implement**：最小改动；只碰设计里列出的文件；侧向改动一律不做。
- **Verify**：按 §1.3 的门槛执行并**保留证据**（命令输出、日志、对比结果）。证据优于口头断言。

依据：Anthropic《Claude Code best practices》的 Explore→Plan→Implement→Commit 与
"give a way to verify its work"；本项目 `native-cpp-migration-plan.md` 的批次门禁实践。

### 1.2 变更分级与设计产物

| 级别 | 判定 | 设计产物（最小） |
|---|---|---|
| **S** | 单文件内的注释/拼写/纯格式；文档笔误 | 无（直接改） |
| **M** | 普通缺陷修复、非攻击路径的功能/重构、测试补充、文档结构性更新 | 一段改动说明：动机、影响文件、行为差异、验证计划（可写在任务描述或提交信息中） |
| **L** | 攻击关键路径（waiter/race/payload/route/exec 流程）；配置文件格式/wire 协议；跨 Native↔Kotlin 契约；公共数据结构；构建系统；新增 route | **计划文档**（模板见 `documentation-standards.md`）：现状与基线 commit、目标与约束、改动清单（逐文件）、数据流/控制流差异、兼容性与回滚、验证矩阵、明确保留项 |

L 级改动**必须**先给出设计并获得用户认可，再写代码。历史范例：
`DECOUPLING_PLAN.md`（Goals and constraints → Phase 0 基线 → 逐项 checkbox）；
`docs/development/native-modernization-plan.md`（批次化 + 每批验证 + 明确保留）。

### 1.3 验证门槛（按级别）

| 改动 | 主机测试 | NDK 构建 | lint-tidy | `cmp_disasm` | 真机门禁 |
|---|---|---|---|---|---|
| S/M 非攻击路径 | 必须 | 必须零警告 | 必须 0 findings | 可跳过 | 不需要 |
| M 触及攻击路径 | 必须 | 必须零警告 | 必须 0 findings | **必须**（8 函数） | 必须（见 §8.3） |
| L | 必须 | 必须零警告 | 必须 0 findings | **必须** | 必须，且归档证据 |

命令见 `AGENTS.md` 与 §8。

### 1.4 批次化与"明确保留"

- 大改动拆批次，**一个批次只做一类事**；每批次独立完成"实现 → 全部门槛 → 记录验证结论"，
  上一批未验证通过不得进入下一批。
- 每个计划**必须**包含"明确保留（不做）"清单，防止顺手重写（例：`futex_hash.h` 内核移植代码、
  `put32/put64`、`PayloadPage` 无析构、`FIELD` 宏）。未列入改动清单的代码即视为冻结。

### 1.5 提交与分支

- 分支流向：`very-not-stable-dev` → 上游 `main`。**未经用户明确要求不得 commit / push / 建 PR。**
- 提交信息用 Conventional Commits：`type(scope): summary`，`type ∈ {feat,fix,docs,refactor,test,chore,build,revert}`；
  行为变化与不变量写进 body（历史范例：`fix(profile): resolve imported v1 reports and edit them per their own release`）。
- 一个提交只表达一个意图；不夹带无关格式化；大变更拆成小提交序列（对应批次化）。
- 评审：主库变更应由至少一名非作者复核（历史 PR review 即此实践）；L 级改动以计划文档 + 门禁证据
  作为评审记录；新增功能不带测试视为不完整。

---

## 2. 架构规范

### 2.0 目标架构（重写进行中；权威 = ADR-0001/0002/**0004**/**0006** + `top-level-architecture-rewrite-plan.md` + 现行批次 `branch-plan.md`）

> **选择权威（2026-10-05）**：装配选择由 **token 白名单**表达（`contract::kCombinationCatalog`：token → {backend, route, steps, terminal, available}），
> token 落在 `backend.<id>.steps`；`pipeline` 只做分派。**词汇/白名单必须导出并对拍**（native → 资源 → Kotlin agreement test），
> 不得在 Kotlin 侧手写第二份。**terminal 归属见 ADR-0006**（词汇保留、实现下放 backend、共享件中性）。

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
  │  ProcessBuilder（不是 JNI）：stdin 写 GLK1、stdout/stderr 收日志、退出码表达结果
  ▼
src/core/ (C++23 可执行 ELF)          tools/extract_rs/ (Rust 离线提取器)
```

- 层间契约**只能是**进程、文件、stdin/stdout、退出码、以及 GLK1/HOCON profile 文本；新增跨层状态
  必须经 profile 或文件，不得发明新环境变量（§3.1）。
- Kotlin 分层：`ui → domain → data`；平台 I/O 只允许在 `data/`；`shizuku/` 是执行入口的变体，
  不承载业务规则。
- Native 命名空间分层：`ghostlock::{route,session,memory,attack,race,support,profile}`（**重写目标见
  §2.0**）；include 一律相对 `src/core`（`#include "route/..."`），让所有权在调用点可见。

### 2.2 route 结构（本项目核心扩展点）

> 现状描述；重写后 `route` 为 **backend 内部策略**（见 §2.0 / ADR-0001）：`Pipeline<Backend, Terminal>`，
> route 由 backend 按 profile 选择；terminal 由 pipeline 编排（R12）。

每条 route 由三个部件构成（**可使用虚基类**；但 PI 竞争窗口内的间接调用会引入不可预测时序，见 §4.2）：

1. **Policy**（`route/route_policy.hpp`）：编译期能力声明 + `kind` + `supported()` + `run()`。
   **必须 host 可编译**（主机测试引用每个 Policy）。
2. **Procedure**（已退役：原 `session/exploit_procedure.hpp`；执行策略现由 backend 步骤 + route policy 承担）：
   只覆盖与共享流程不同的步骤；共享步骤（setup/W1/terminal/retry）严禁复制到 route 内。
3. **Route 类**：`prepare → execute → disarm → destroy` 固定生命周期（概念见 `route_lifecycle.hpp`），
   结果只经 `status` 汇报。`execute` 仅在 `prepare` 返回 0 时调用。

新增组件（route/backend/terminal/platform/ancillary）的完整触点清单以 `docs/development/adding-a-component.md`
为唯一权威，必须逐项核对。

### 2.3 编译边界与条件编译

- Policy、数据结构、纯函数**必须**能在主机（非 Android）编译；真机专属实现放在
  `#if defined(__ANDROID__)` 段内（route 的入口/Procedure/工厂即在其中）。
- 主机测试由 `src/Makefile` 的 `NATIVE_HOST_TESTS` 登记；测试专用探针不得链接进生产二进制。

### 2.4 全局状态

- 唯一进程 singleton 为 `g_exploit_session`（**重写后为组合根 `CoreSession`**，能力句柄存
  `CoreSession.capabilities`，见 §2.0 / ADR-0002）；另允许启动期只读的 `g_direct_map_end`。
  **新代码不得引入任何可变全局或进程级引用别名**（审计口径见 git 历史 `docs/analysis/native-global-state.md`）。
- route 级 static 属于受限例外，必须：匿名 namespace、单例访问器、`stop()` 后归零，并在计划中说明。

### 2.5 上游移植代码与 v1 legacy

- `kernelsnitch/`（含 `futex_hash.h`、`utils.h` 的 `pr_*` 宏）是上游移植代码，**上游已冻结、不再更新
  → 可改写**；但必须在**顶级架构重构拆分之后**作为 backend `leak` 模块进行，并保测试，不得顺手改
  （见 ADR-0001 §17 与 `AGENTS.md`）。
- v1（旧 `offsets.json`）只在 Kotlin 侧由 `LegacyProfileConverter.kt` 转换；native 不再解析 v1。新 route/新字段
  **不得**修改 legacy 转换；兼容输入只做一次性转换。

---

## 3. 数据流规范

### 3.1 配置数据流：单一权威

```
assets HOCON（index.conf + <uname-r>.conf + execution-*.conf + 公共节）
   → Kotlin 解析/合并（include 展开、route 显式选择）
   → 用户覆盖层（按 release 稀疏保存，优先级高于内置与导入）
   → GLK1 二进制（stdin 传给 native；exportProfiles 生成 .bin）
   → native TargetProfile 只读快照（此后执行层只认它）
```

- **profile 是唯一配置权威**。执行层只允许读进程/路径类环境变量（`GHOSTLOCK_HOME`、`TMPDIR`、
  `GHOSTLOCK_KSU_LOG`）；需要新状态就扩展 profile，绝不新增配置类环境变量。
- `uname -r` 精确匹配；route 由 profile 显式声明，**不得**从 kernel 版本/设备名推断。
- 导入与内置的差异必须可追踪（字段级比较后再提供覆盖），profile 的 release 名与文件名逐字一致。

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

---

## 4. 控制流规范

### 4.1 攻击阶段机

```
setup → W1（SELinux）→ W2（凭据）→ W3（seccomp）→ handoff（root child / KernelSU）
```

- 每阶段返回结构化结果，重试统一走 `retry_write_stage()`；**不得**在 route 或阶段里各自实现重试。
- W1/W2/W3 的目标地址与写模式来自不可变 `WriteRequest`；stage 之间不共享可变中间态，
  共享状态只经 `CoreSession` 显式传递；terminal 输入用 `terminal::RootedChild`（ADR-0004 R10）。
- handoff 后的子进程所有权经 `release_child()` 显式转移；探针失败走"退休"而不是盲写。

### 4.2 PI 竞争窗口规则

`waiter_thread → owner_thread → consumer_thread → CMP_REQUEUE_PI → route` 窗口内：

- **禁止**异常、锁、日志阻塞调用（用 `log_sync()` 语义的既有路径）；**避免**间接调用（虚函数/`std::function`）——
  **非禁止**，但它会在窗口内增加不可预测时序；若使用，须以真机门禁验证（§8.3）；
- 只允许直接调用与编译期已知的 Policy 能力比较（`route_capability`）；
- 任何"顺手简化"都必须经真机门禁验证（§8.3）；`cmp_disasm.py`（§8.2）是**可选诊断**，用于定位机器码差异范围。

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
- 能力（§2.0）以“空句柄”表达不可用，调用方 fail-closed；**异常与间接分派绝不进 PI 窗口**（E2/E3）。

### 4.4 状态/投影字段语义（UI 与状态设计；从真机缺陷提炼，**强制**）

> **来由**：同一模式在 24 小时内连续造成 3 次真机缺陷（`recommend_shizuku` 升级即崩、`enabledPlugins()` 冷启动抛异常、插件开关「关掉后自己变灰」）。缺陷与修复记录见 `../analysis/branch-plan.md` 的「规则溯源」条目。

1. **一个投影字段只表达一个语义**：**禁止**把一个为某用途计算的布尔（「可用性/有效性」）直接当作**另一个**用途的判据（如「控件可交互」）；跨用途复用必须**另立字段并命名**。
   示例：`runUsable`（本次运行是否可用 → 运行级选择门控）与 `toggleable`（用户能否切换 → 只要**已安装**即 true）必须分开；`selected`（是否参与本次运行）与二者都不同。
2. **UI 控件不得因「数据缺失」而变成不可交互**，除非缺失本身意味着**不可操作**；否则用户无法自救——**「关掉就再也开不回来」是最坏的一类**。错误应表现为**可见的诊断行**，而不是禁用控件。
3. **失败要结果化，不要异常化**：**高频路径**（构建文档、构建列表、刷新状态）**不得抛异常**中断 UI/进程；用结果类型（`Ready` / `Blocked` + 原因）表达，并让原因**可见**。
4. **外部/历史数据一律容忍 + 诊断**：只有**本版本自己撰写**的输入才 fail-closed；旧版本遗留数据的校验失败**不得**升级为 `require`/异常，必须**容忍 + 记诊断**。
5. **缓存必须有明确的填充时机**：若某缓存只由「某个页面动作」填充，则**任何依赖它的路径在冷启动后都会空**；填充点要么在**初始化**，要么**按需解析**。

**反面案例（均已修；提交号见 branch-plan）**

| 真机缺陷 | 错误做法 | 正确做法（对应规则） |
|---|---|---|
| `recommend_shizuku` 升级即崩 | 把「**旧版本遗留数据**的验证」写成 fail-closed 的 `require`（`ProfileLayout.validateLegacy`，`profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:480`） | 旧数据**容忍 + 诊断**；只有本版本撰写的输入才 fail-closed（规则 4） |
| `enabledPlugins()` 冷启动必崩 | 把「**构建文档的失败**」实现成**异常**，且描述缓存只在插件页动作里填充 | **结果化**（`Ready`/`Blocked`）+ 错误可见（规则 3 + 5） |
| 插件开关**关掉后自己变灰**（用户原话：「插件关掉之后开关变灰无法再次开启，只有退出到其他界面才能开启」） | `PluginPresentation.kt:235` 的 `selectable = descriptor != null && errors.isEmpty()` 被当作 `PluginSettingsUI.kt:174` 的**控件可交互性** ⇒ 一关掉就没有 descriptor ⇒ 开关被自己禁用，用户无法自救 | 拆成 **`runUsable`**（运行级选择门控）与 **`toggleable`**（只要已安装即 true）；错误以**诊断行**呈现（规则 1 + 2） |

---

## 5. 数据结构规范

### 5.1 内核 ABI 结构（最高风险区）

- **GLKv3（MessagePack）为迁移目标格式**：权威设计见 `docs/analysis/wire-transport-model.md`；native 侧 `profile/glkv3.*`（MPack）、
  Kotlin 侧 `data/profile/Glkv3Encoder.kt`；v2 迁移期只读。
- `profile/binary.cpp` 的 GLK1 wire v2 section/key 表（迁移期）是**契约权威**：字段名、类型与位型一经发布不可改；
  新增字段 = 在该 section 追加一个条目（section/entry 顺序无关），并与 Kotlin `NativeProfile.kt` 同步。
  **重写后分层**：framing（magic/version/组件 id/sections）中性；`sections → TargetProfile` 的绑定归
  **backend**（见 ADR-0001 §12/§17，F6）。
- Kotlin↔Native 双侧注册表（`RouteKind`、字段表）由测试锁定（`route_catalog_test` /
  `RouteCatalogAgreementTest`）；键名逐字一致，顺序逐项一致。
- 枚举带显式 wire 值并写入合同注释（`0` 值语义特殊时不得被当普通值使用，如 `kRouteAuto` 不出现在 Kotlin）。

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

## 6. 代码风格规范

### 6.1 C++（`src/core`）

- 语言：C++23、`-fno-rtti`、静态 libc++；`-O2 -flto`；`-Wall -Wextra -Wconversion -Wsign-conversion`，
  **新代码不得引入任何告警**。禁止 RTTI/异常/非标准扩展。
- 头文件：`#ifndef GHOSTLOCK_<PATH>_<FILE>_HPP` guard、自包含、`#include` 相对 `src/core`
  （项目头）、组内按字母序（沿用现有文件的实际风格，不做无关重排）。
- 命名：函数/变量 `snake_case`；类型 `PascalCase`；编译期常量 `kCamelCase`；
  成员变量 `name_`；宏仅限已保留的移植头。
- 格式：4 空格缩进、K&R 大括号、行宽 ~100；`clang-format` 未强制（`FormatStyle: none`），
  但同一文件内保持一致。
- 注释：写**设计意图与不变量**（为什么），不写"是什么"。既有 `/* ... */` 段落注释是范例
  （见 `route_policy.hpp` 的 Policy 说明）。
- 接口：单参构造与转换运算符必须 `explicit`（有意充当值语义时除外）；不可变性优先；
  编译期检查（`static_assert`/concept）优先于运行期检查。
- clang-tidy：`bugprone-*`、`performance-*`、`clang-analyzer-*`，`--warnings-as-errors='*'`，
  期望 0 findings；category 级排除必须写入 `.clang-tidy` 注释说明理由；单点误报用行内 `NOLINT`。
- 静态/全局对象：只允许平凡析构；动态初始化必须有明确理由（对照 Google Style 的同条规则）。

### 6.2 Kotlin（`app/`）

- 遵循官方 Kotlin Coding Conventions（4 空格、trailing comma、表达式体、modifier 顺序、
  命名规则）；项目内额外约定：
  - 默认 `internal`/`private` 收紧可见性；`val` 优先、immutable collection 优先；
  - profile 相关类以 `data class` + `entries()/apply()/from()` 三元组为模式；
  - 与 native 同步的常量/枚举注明对应 native 符号（如 `RouteKind` 的 wire 注释）；
  - 注释使用 KDoc 简述意图，不用 `@param`/`@return` 复述签名。
- 平台 I/O 必须隔离在 `data/`；领域逻辑放 `domain/usecase`；Compose UI 不直接碰进程与文件。

### 6.3 Rust（`tools/extract_rs`）

- `cargo fmt` + `cargo clippy` 作为默认门槛；错误处理用 `Result` + `?`，不 `unwrap` 可失败路径。
- API 面向本仓库的 CLI 契约：`--format conf`（flatten GLK profile，默认）与 `--format json`
  （v1 导入）、退出码有语义（如 preflight 拒绝 `6`）；新增 CLI 选项要更新 README 与 profile
  适配文档。

### 6.4 提交、分支与评审批次

见 §1.5。评审批次参考 `native-cpp-migration-plan.md`：每批次一个可独立验证的提交序列，
提交信息含变更与验证结论。

### 6.5 构建脚本：Gradle-only 与跨平台（用户指令 2026-10-05）

- **构建逻辑一律写进 Gradle KTS**（`*.gradle.kts`）：LKM/DDK、插件产物、native 准备等都由 Gradle 任务承担；**禁止独立 `.sh` 构建脚本**。
- **`.sh` 仅限设备端与运维**：例如 `tools/lkm/ghostlock/root_cmd.sh`（经 `call_usermodehelper` 执行的设备载荷）、`tools/device-guard/*`、`.github/scripts/*`；**第三方/参考项目自己的 `build.sh` 与本规约无关**，文档中照原样引用。
- **跨平台硬要求**：不得依赖 `shasum` / `sha256sum` / `mkdir -p` / `mv` / `cp` / `find` / bash —— 一律用 JVM/Gradle API（`MessageDigest`、`Copy`/`Sync`、`FileTree`）。
- **工具链路径不依赖 PATH**：如 `llvm-objcopy` 由既有 NDK 解析器（`android.ndkDirectory`，见 root `build.gradle.kts`）定位；prebuilt host tag 用通配。
- **容器引擎**：探测 `podman` → `docker`，允许 `-PcontainerEngine=` 覆盖；需要容器的任务（如 `buildLkmImages`）必须**显式**执行，**不挂** `preBuild`。
- **跨端列表不得手抄**：8 个 KMI label 由 native 导出 `lkm-kmi-manifest.tsv`（两份逐字节一致）供 Gradle 与 Kotlin 消费——与 `profile-manifest-v3.tsv` / `combination-manifest.tsv` / `vocabulary-manifest.tsv` 同规。

---

## 7. 文档规范（摘要）

完整规范——文档分类与地图、命名与结构、中英文写作、RFC 2119 用语、计划/门禁模板、归档与评审流程、
作者检查清单——以 [`documentation-standards.md`](documentation-standards.md) 为唯一权威（依据
ISO/IEC/IEEE 42010 / 15289 / 2651x、Diátaxis、DITA 信息类型、Carroll Minimalism、Google style）。
工程侧的三条硬约束：

- 双语对（`README.md`/`README_ZH.md`、`docs/profile/**` 的 `_ZH`）必须同批次同步修改；
- 改动影响命令/字段/流程时，文档必须同一批次回写；
- 计划文档与门禁记录的模板字段（含 commit 与日期）必须完整，fail/pass 同等归档。

---

## 8. 验证与证据规范

### 8.1 验证金字塔

1. **主机单元测试**（`make -C src native-host-tests`，全量含 host 数据流 harness）：固定向量、
   生命周期、几何边界、双侧一致性；新增数据结构/route 必须补测试（登记进 `NATIVE_HOST_TESTS`）；
   无对应测试的功能变更视为不完整。
2. **Kotlin 单测**（`./gradlew :app:testDebugUnitTest`）与 **Rust 测试**（`cargo test --release`）。
3. **形状对比**（攻击路径专用，§8.2）。
4. **真机门禁**（§8.3）。

### 8.2 攻击函数形状对比（可选诊断，**非门槛**）

```sh
python3 tools/cmp_disasm.py <baseline-binary> build/native/ghostlock
```

- 覆盖 8 个攻击函数（`TARGETS`）；用于**定位机器码改动范围**（哪些攻击函数受影响、差异是位移还是形状）。
- **不是门槛**：反汇编差异本身不阻塞批次；**判据是真机门禁（§8.3）**。新攻击函数可加入 `TARGETS` 以便日后诊断。
- 若跑了，结论作为定位信息写入提交信息/计划文档，而非准入条件。

### 8.3 真机门禁（攻击路径改动必须）

**前置**：冷机、固定 CPU 对、单 route、KernelSU 未加载的干净启动、设备状态记录（uptime）。
**判定**：`route_done status=0 clean=1/1`、`child is root!`、`exploit complete`、handoff `KernelSU ready`、
无 kernel panic；至少重复运行并记录次数。
**归档**：按 `documentation-standards.md` 的模板写记录；日志对应设备 `Download/ghostlock-debug-log/<时间>/`。

### 8.4 归因纪律

- `KERNEL-PANIC-01` 是已知环境/时序问题（同构建可出现 PASS/panic/PASS）。判定代码因果**必须**
  同构建复现 + 冷机复跑 + 栈证据；单次 panic 不得归因代码。
- 未验证路径不得宣称支持：Multicast（5.15）、TCP、Select 三条路径均已由开发者真机验证；
  新 profile 未过真机不得标 `supported`。
- 失败记录与通过记录**同等归档**（fail/panic 文档与 pass 文档并存，格式一致）。
- **「已落地」必须绑定真实提交（强制）**：文档不得把**工作树状态**写成「已落地」——否则文档比代码超前，别人按文档去 `git show` 会扑空。未提交的实现只能写「**已实现（工作树，待提交）**」，并在**提交后回填提交号**；**归因只写实际包含该改动的那次提交**（本项因一次真实误归因而设立：把「对齐」记到了实际是「旧拼写」的那次提交上）。

---

## 9. Agent 执行检查清单（先设计，后改动）

**开始前**
- [ ] 读 `AGENTS.md` + 本文 + 与任务直接相关的现行文档（`adding-a-component.md`、PROFILE_SCHEMA 等）
- [ ] 用 git 历史检索是否已有同类设计与证据（`git log --all -- <path>`）
- [ ] 判定变更级别（S/M/L）；L 级先写计划并获用户认可
- [ ] 列出影响面：文件清单、数据流/控制流/数据结构是否变化、是否需要跨层同步
- [ ] 确认不变量（profile 权威、全局状态清单、wire 兼容、保留清单）

**实施中**
- [ ] 最小改动；不碰设计外文件；不动 `LegacyProfileConverter.kt`/保留清单；`kernelsnitch` 仅可在顶级重构中作为 backend `leak` 模块改写（先设计+测试，见 §2.5）
- [ ] 双侧同步：Native↔Kotlin 注册表与字段表逐字一致；文档同步（含 `_ZH`）
- [ ] 新代码零告警；显式类型转换；RAII 所有权；不可变性优先

**完成后**
- [ ] 按级别跑满 §1.3 门槛命令，保留原始输出
- [ ] 攻击路径：真机门禁 + 门禁记录归档（`cmp_disasm` 可选诊断）
- [ ] 汇报时给出**证据**（命令、结果、文件路径），不写"应该没问题"
- [ ] 未经明确要求：不 commit、不 push、不建 PR

**禁止模式**（历史失败模式）
- 厨房水槽式改动（多主题混杂、顺带重构）
- 无设计直接改攻击路径；改了不跑形状对比
- 无证据宣称验证通过；用单次结果推翻既有归因
- 把历史文档当"过时垃圾"删除而不留索引；重复造已存在的抽象

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

- **单写入流**：同一时刻只允许一条写入流改共享工作树；并行工作限只读。批次落地必须**原子提交**。
- **导出对拍**：跨语言词汇表（route 目录 / GLKv3 字段 / **组合 token 白名单** / 可用性）只有 native 一份权威，
  必须导出到资源并由 Kotlin 运行时读取 + agreement test；Kotlin 源码中的 token 字面量只允许出现在测试里。
- **lint 红线**：`clang-analyzer-optin.performance.Padding` 等以 `-warnings-as-errors` 生效；新增公共结构体必须一次排好字段序。
