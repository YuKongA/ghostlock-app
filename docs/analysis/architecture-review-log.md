# 架构审查记录（Architecture Review Log）

> 归档用途，**不是权威设计**。当前权威：`adr/0001`–`adr/0004` + `top-level-architecture-rewrite-plan.md` +
> `exploit-session-generalization-plan.md`。
> 本文件保存历轮审查的原文（含已被推翻的结论），仅供审计与追溯。

## 一、来自《顶级架构重写 计划》

## 架构审查（第三轮，2026-10-03）

对目标结构/ADR-0001/0002 做了一轮设计审查，发现以下结构性问题（多数落在 Phase A2，不阻塞 Phase 0，
但必须在进入 A2 前闭环；ADR-0001 需据此修订）：

**A.（方向已定）`profile` = 可表达任意路径任意参数的中性 document + 各 owner 注册自己的 schema。**
设计要求 profile 能完整表达任意 route/path 的任意参数，所以**包含具体后端参数是必须的**；正确解法不是把参数
从 profile 移走，而是：中性容器只存 `sections[name→fields[name→u64]]` + presence（GLK1 v2 已如此），
**backend 与 platform 各自注册字段 schema/typed view**，对同一 document 纯派生，**每个字段恰一个 owner**。
待细化（仍属未决，需设计）：

- 注册机制：建议编译期 schema 描述表（section/field/类型宽度/必填）+ 模板遍历，不引入可变全局；
- 字段归属划分：如 `task.cred` 归 `platform::abi`，backend 消费其 view；禁止双重声明；
- 三端一致性：native 注册表 ↔ Kotlin（`profile-core`/编辑器）↔ extractor `symbols.rs` 的映射需各自

  单点并由测试对拍（P6“一处字段映射”的落点）。

- **落地方案见 `docs/analysis/adr/0003-profile-schema-registration.md`**（Document/Schema/注册机制、初始

  字段划分表、strict 未知字段策略、三端 manifest 对拍、验证）。

**B. `memory` 仍硬依赖 `profile::TargetProfile`。** 证据 `memory/address_space.h:4,28,30,45`
（`init(const TargetProfile*)`/`init_for_soc`/`soc_name`）。目标 DAG 说 memory 是中性地址数学，但当前 API
把 memory 绑在 43499 模型上。Phase A2 的 `AddressSpace` 拆分若不改这些签名，`memory → profile` 依赖仍在。
要求：`AddressSpace` 只收中性值（soc/phys）；profile→值的绑定上移到 backend/platform。

**C. ancillary 的“中性框架 + 行为注册”没有机制。** 证据 `ancillary_controller.hpp:15` 在中性控制器里硬编码
`std::tuple<VrGuardPolicy, VrTaskTagPolicy>`（platform 行为）；`AncillaryKind` 也在 `session/ancillary`。
ADR §14/A2 要求行为由 platform/backend 注册，但未给注册方式。要求：注册表以模板参数/类型列表由组合点注入；
`AncillaryKind` 中性化或用 concept 取代枚举。

**D. handoff 与 backend terminal 的边界未定，`VictimChain` 泄漏。** 证据 `route/frontend_contract.hpp:64`、
`session/root_child_frontend.hpp:26` 都用 `session::VictimChain`。ADR §7/F17 说泛化或 terminal 归 backend，
但未定。要求：二选一并写清——handoff 只收中性窄视图，victim 语义/终止选择留 backend；或 root_child terminal
整体归 backend，handoff 只留通用进程/脚本机制。

**E. `CoreSession` 定位：组合根还是 service locator。** ADR §5 称 CoreSession 为组合根，但装配（backend 状态
构造、能力建立）实际在 main/orchestrator。把所有东西（runtime/selection/AddressSpace/Capabilities/backend 槽）
挂在一个任意组件都拿得到的对象上，有重新变成 god object（P1）的风险。要求：明确“组合根 = main/orchestrator
（装配），CoreSession = 状态容器”；让 backend 状态成为 backend 的关联类型（`B::State` + `B::state(session)`），
使 orchestrator 构造与 pipeline 分发不可能错配。

**F. 能力（Capabilities）可能漏进 PI 热路径。** “只在 PI 窗口外经函数指针调用”目前靠约定：route/primitive/attack
都拿 `CoreSession&`，也就能取到 `Capabilities`。要求：类型层隔离——route/primitive/attack 的接口**不得出现
Capabilities**，只有 ancillary/terminal/service 收 `Capabilities&`；加 host 断言/检查锁住。

**G. route 私有化 与 Kotlin 单一 route 枚举的跨层冲突。** native `profile/model.h:40-70` 有 `RouteKind`/`kRouteCatalog`；
Kotlin `profile-core/.../data/route/RouteKind.kt` + `Profile.kt:34-38` 用全局 `document.routeKind`。ADR §16/§17 F11
要把 route 移入 backend，但 wire 只有**一个全局 route 字段/id 空间**，且 `RouteKind.kt` 在共享的 `profile-core`。
要求：区分“中性 wire route id”与“backend 的 route 语义/能力”；Kotlin 侧标明哪部分中性、哪部分随 backend；
一致性测试参数化到 backend（(backend, route) 对），否则“route 私有”只是目录搬家。

**H. catalog ↔ backend 编译期环仍未排期。** ADR 自述已知遗留：pipeline catalog 枚举 backend 身份，backend 又
include catalog 做 static_assert。重写后此环仍在；ADR 给的缓解方向（身份声明在 component、注册表在 pipeline
不回 include）没有对应的 Phase/测试条目。要求：在 Phase A 明确消除或加断言/测试，别留在“已知遗留”里带进后续。

**I. 小问题（命名/分层）。** `profile`（容器）与 `backend::...::profile`（模型）同名不同层易混；
`SocFamily`/`detect_target_soc`（设备事实）落在 `memory`（地址数学）里，与 `platform` 职责重叠；
`support::fail_stop_dirty_race` 等偏 43499 race 语义，是否真通用需复核。

**架构审查结论：** 目标方向（按变化轴切、backend 私有机制、中枢性 framework）成立。A 的归属方向已定
（中性 document + owner 注册；见 ADR §10/§12），但注册机制/字段归属/三端一致性仍待设计；B/C/D/G 属
“不解决就会在 A2 处卡住/倒退”的结构问题，需先修订 ADR-0001/0002 再进 A2；E/F/H 属机制/防错，需给出类型或测试。
Phase 0（session 通用化）本身不依赖这些结论，可继续。

## 大框架审查（第四轮，2026-10-03）

**结论先行：** 顶层切分沿“变化轴”的方向是对的（Parnas §2），backend 私有机制、`Pipeline<Backend, Terminal>`、
profile document+注册都成立；但**框架表述与依赖模型存在几处不自洽**，建议在动手 Phase A 前收敛，
否则 A2 会带着结构性含糊往下走。

**成立的部分**：backend 私有 primitive/route/victim/service（survey 证据）；`Pipeline<Backend, Terminal>`（route 内部化、terminal 由 pipeline 编排）、
route 由 profile 在 backend 内选；profile = 中性 document + owner 注册（ADR-0003）；编译期能力 `constexpr`、
PI 窗口零间接、单可变全局。

**F1 依赖模型不是层次链，而是有环的组件图。** 计划写的 `support/kernel/profile → contract → memory → session
→ pipeline/backend/ancillary/handoff` 无法表达：`pipeline ↔ backend`（catalog 与身份互 include）、
`platform`/`handoff`/`ancillary` 没有位置、`memory` 依赖 `kernel` 的地址类型。建议以**允许依赖图（allowed edges）**
为权威，配 include 防火墙测试；层次链只作叙述。

**F2 “四条正交轴”名不副实。** route 已明确在 backend 内；`platform` 与 backend 也不正交（backend 只支持特定
platform）。**裁决（R2/A1）**：改为 **2 装配轴（backend × terminal）+ backend 内 route + 2 横切（platform ·
capabilities）**，不再并列为同级轴。

**F3 轴命名与顶级命名空间错位：`terminal` 缺顶级名，`handoff` 是策略不是轴。** 建议顶级改 `terminal/`
（`root_child`/`umh`/`file`/… 策略），`handoff` 降为 `terminal::root_child` 下的机制；否则 43503（写文件）/
23274（panic）在框架里无处安放。

**F4 `ancillary` 更像机制，不是轴。** 现仅 2 个 vivo vr 行为，且行为属 platform；把它升为顶级框架，成本/收益
待证。**裁决：保持顶级**（中性机制，行为由 platform/backend 注册），见 ADR-0004 R4。

**F5 `platform` 混三类东西**：只读数据（ABI 偏移/设备 phys）、厂商对策（vivo）、设备运行时探测
（`apply_iomem_cache`/`check_selinux_off`/`process_has_seccomp`）。建议内部拆 `platform::abi`（数据）/
`platform::runtime`（探测）/`platform::vivo`（厂商），否则是新的 mini-god。

**F6 `kernel` 与 `memory` 边界模糊**：kernel = 地址布局常量/`KernelAddress` 域/struct page；memory = 地址数学；
两者相邻且 memory 依赖 kernel。建议合并为 `memory`（内含 kernel 词汇）或明确“kernel = ABI 词汇、memory = 运算”。

**F7 单次门禁 vs 项目“批次验证”原则冲突。** AGENTS 要求“大改动按批次推进，上一批验证通过再进下一批”，
而计划“重写期间不做每步 IDENTICAL，以最终门禁为准”把安全网撤在 A2（语义收敛，确有行为风险）上。建议：
A（机械搬迁）可分小批、每批 host+NDK；**A2 必须独立门禁**（host + NDK + cmp_disasm + 真机），否则失败
无法归因。

**F8 中立性/可扩展性没有机械验证。** “中性件必须能被原语不同的 backend 复用”目前无测试。建议：a) include
防火墙测试（中性头不得拉 backend/platform 头）；b) 一个 fake backend stub（不同 State/route）编进 host 测试，
作为可组合性证明。

**F9 同族复用（UAF）没有落点。** survey 指出同族可抽“利用框架接口”，框架未留位置。建议记为“有第二 backend
时再抽”，但要在框架文档里写明这个决定，避免读者以为漏了。

**裁决（2026-10-03）**：见 `docs/analysis/adr/0004-framework-convergence.md`（**R1–R9 已定**，维护者授权直接裁决）。
本文“目标结构”树与依赖说明已按 R1–R6 更新；门禁按 R7。

## 大结构复核（第五轮，裁决后，2026-10-03）

对 ADR-0004 裁决后的结构再核，发现以下新暴露/遗留的不一致，需在 Phase A 前定（S1–S8）：

**S1 结果类型的归属自相矛盾。** 目标树把 `RunResult`/`RunCode` 放 `contract`，映射表把 `RunResult`/`RunCode`/
`RunStage` 放 `pipeline`，而另一行又把 `RunCode` 放 `contract`。**已修**：`RunResult`/`RunCode`/`RunStage`
→ `pipeline`（pipeline 结果），`StageResult` → `contract`（组件步骤结果）。

**S2 概念分层：identity 与 execution 必须分开。** 现状 `BackendIdentity`/`BackendExecution` 与 `FrontendKind`/
`BackendKind` 都在 `route/component_catalog.hpp`（→ pipeline）。若整体归 `contract`，`BackendExecution` 因引用
`CoreSession` 会让 `contract → session`（违反 R1）。裁决：**身份词汇与 identity 概念**（`FrontendKind`/`BackendKind`、
`BackendIdentity`）→ `contract`（backend 可 static_assert）；**执行概念**（`BackendExecution<B>`/`FrontendExecution<F>`，
含 `CoreSession&`）→ `pipeline`（只在组合点实例化时检查，backend 不引用）。

**S3 `CoreSession` 只能放中性字段。** 若它放 `platform::abi::View`/backend `View` 等 typed 字段，就产生
`session → platform/backend`（违反 R1）。裁决：`CoreSession` = `RuntimeConfig` + `contract::Selection` +
`memory::AddressSpace` + `contract::Capabilities` + 不透明 backend 槽；`Selection` 的词汇（`BackendKind`）在
`contract`。

**S4 terminal 输入契约（已定）。** 定义中性 move-only `terminal::RootedChild`（pid + command fd +
`alive`/`seccomp_bypassed`/`ever_rooted` + 显式 `retire()`/`detach()`）；backend 在边界由 `VictimContext`/
`VictimChain`/parked 状态**转移**构造，terminal 消费；不再传 `VictimChain`。见 ADR-0004 R10。

**S5 “bootstrap/装配”层未进树。** ADR-0001 §9 提到 `install_profile`/`resolve_profile_addresses`→bootstrap，但
目标树没有它。裁决：不新增顶级，装配（Document 解码、view 绑定、`AddressSpace` 构造、backend 状态构造）归
`pipeline`（可作 `pipeline::bootstrap` 子模块）。

**S6 命名冲突（已定）。** backend 内与顶级同名的子模块加 `backend_` 前缀：`backend_profile`、`backend_terminal`。
见 ADR-0004 R11。

**S7 catalog 保证要补测试。** `Pipeline<Backend, Terminal>` 取消了原 full-triple 的 `combination_supported` 保证；需
“每个 catalogued backend ↔ 恰好一个 `Pipeline<B>` 实例、`dispatch_target` 一一对应”的测试。

**S8 Document 归属与允许边。** pipeline 负责解码/绑定 Document，故需 `pipeline → profile`（已补进 ADR-0004 允许边）；
backend 状态由 pipeline 经 `B::state_from(document)` 构造后存入不透明槽，`run(CoreSession&)` 保持无 Document 参数；
session 不持有 Document。

**结论**：S1 已修；S2/S3/S5/S7/S8 是 R1 的直接推论，建议按上述裁决固化；S4 与 S6 需你定（S4 是 handoff/terminal
边界的实质，S6 是命名）。

## 大结构复核（第六轮，2026-10-03）

对 ADR-0004（R1–R11）落定后的结构再核，仍有以下未闭环（S9–S15）：

**S9 terminal 既被写成“选择维度”，又被写成 backend 内部（矛盾，需裁决）。** R2/R3 把 terminal 列为 3 个选择
维度之一与顶级轴；但 ADR-0001 §16 与计划写 `ComponentSelection → {BackendKind}`、“终端由 backend 定”，且允许边里
`pipeline` 不依赖 `terminal`。**裁决（R12）：采用 (a)**——pipeline 编排 `Pipeline<Backend, Terminal>`，新增
`pipeline → terminal`，`ComponentSelection={BackendKind,TerminalKind}`；backend `run(session, RootedChild&)` 产出终端输入，
再 `T::run(session, RootedChild&)`。

**S10 `frontend → terminal` 改名缺清单。** 涉及 `FrontendKind`/`FrontendExecution`/`FrontendIdentity`/
`frontend_available`/`frontend_name`/`kFrontendRootChild`/`kFrontendUmhForward`、GLK1 header 字段 `frontend`、
`AncillaryStage::PreHandoff`、`run_state` 阶段名（↔ Kotlin `RunStateCodec`）。wire 字节不变，但字段语义与命名要同步；
step 名是否改需与 Kotlin 一起定（可能触发行状态码）。

**S11 组件 id 常量位置与 ADR-0003/R7 冲突。** `profile/binary.h` 仍有 `kFrontendRootChild`/`kBackendCve*` 与
`frontend_known`/`backend_known`；ADR-0003/R7 定“容器只搬运 u16、owner 注册并校验”。应把这些常量与校验移到中性
组件词汇（`contract`）或 owner 注册表，并把 `FrontendKind` 改 `TerminalKind`。

**S12 映射表陈旧行（部分已修）。** `session::frontend::{...}`→`handoff::*`、`session::handoff_probe`→`handoff::*`、
`handoff::RootChildPolicy::run(..., VictimChain)` 已按 R3/R10 改；残留 `kernel`/`ancillary` 引用需再过一遍。

**S13 允许边偏宽。** `ancillary → memory`、`profile ← 所有` 等可能超出实际需要；防火墙测试前应收敛到最小边集，
否则防火墙形同虚设。

**S14 具体落点未进清单。** `handoff_probe`→terminal、`victim_process`→backend victim、`root_child_frontend.*`→terminal
的搬迁与构建/测试清单未列。

**S15 `platform` 是“选择维度”还是设备推导（已定）。** `platform` **不是** selection 维度：由 profile 的设备/
厂商/内核段推导，每项对策带 `applicable(profile)` 门控（vr 用 `vr_guard_enabled` 即此形），不新增选择 id。
见 ADR-0004 A2。

**结论**：S9/S15 需裁决；S10/S11/S14 是已定方向的落地清单（可随做随定，但要登记）；S12/S13 是清理。

## 二、来自《Phase 0 计划》

## 重新审查发现与补充（2026-10-03）

对首版计划做了一轮对照代码的复查，发现并已更新以下遗漏（证据均已核对）：

1. **布局偏移已实测**（新“基线布局实测”节）：旧 `profile`=104，`CoreSession` 的 storage 同为 104，
   状态成员逐一对齐 → 绝对偏移可精确复现（`sizeof` 1672）。先前只是推断，现为证据。
2. **参数接收者的字段访问被漏计**：`root_child_frontend.cpp` 的 `session.victim`/`parked_*`、
   `cve_2026_43499_backend.cpp` 的 `session.heap.init()`、`ancillary_controller.hpp` 的 `session.profile`、
   `vr_guard.cpp` 的 `session.profile`/`session.addresses`。迁移必须由编译器错误驱动，不能只搜全局名。
3. **遗漏文件**：`common.h`、`route/route_middleware.hpp`（include 改名）；`ancillary_*.{hpp,cpp}`、
   `vr_guard.*`、`vr_task_tag.*`、`root_child_frontend.hpp`（前向声明）；host 桩与 `ancillary_test`。
4. **构造幂等与对齐不变量**：`cve43499_state_construct` 必须幂等；容量常量须同时 assert `sizeof` 与
   `alignof`；需要 `<new>`（`std::construct_at`）与 `<cassert>`。
5. **`session_layout_test` 改为 `LegacySession` 复刻对照**（不写死数字），并注册进 `NATIVE_HOST_TESTS`、
   更新 Makefile 依赖。
6. **门禁可追溯性缺口**：基线获取方式（`git worktree` + sha256）、真机门禁编号 `RW-00`、
   `cmp_disasm` 偏移位移先例（`kernel-phys-offset-plan`/`5x-tcp-geometry-plan`/`vr-guard-plan`）已补。
7. **文档一致性**：`core/src/README.md` 路径、`engineering-standards.md`、`ancillary-controller-guide.md`
   （“布局固定/不得新增字段”将失效）、`host-attack-dataflow-plan.md`（该计划已归档：索引见 docs/archive/INDEX-plans.md §一）需加现状注；上位计划“`ExploitSession`
   字段布局固定 / 不移动字段”的表述与 Phase 0 矛盾，已改。
8. **上位计划验证矩阵缺 Phase 0 行**：已补。

仍属未知、留待实现期验证：NDK ABI 下 `RuntimeConfig`/`TargetProfile` 的确切 `sizeof`/`alignof`（用同一
探针或 `cmp_disasm`）；`kBackendStateBytes` 的最终取值。

## 深度审查补充（第二轮，2026-10-03）

1. **工作树不是 `acc5e7b`（最高优先，推翻本计划的基线前提）**。`git describe --dirty` = `acc5e7b-dirty`；
   24 个已跟踪文件有未提交改动 + 若干未跟踪新文件，分三组在飞工作：
   - ancillary vr-task-tag 迁移：新增 `session/ancillary/vr_task_tag.{hpp,cpp}`、改
     `ancillary_controller.hpp`/`ancillary_policy.hpp`/`cve_2026_43499_backend.cpp`（vr 写移入行为）、
     `tests/ancillary_test.cpp`、新增 `tests/host/ancillary_stub.cpp`、`core/Makefile`；
   - 占位 backend：新增 `session/backend/cve_2026_{31431,43503,23274}_backend.hpp`、改
     `route/component_catalog.hpp`/`backend_contract.hpp`/`backend_policy.hpp` 与测试；
   - TCP 5.x geometry：`profile/model.h`、`profile/binary.{h,cpp}` 新增 `tcp_*` 字段。
   这些**正好落在 Phase 0 要改的文件上**。**用户确认：在飞改动保留，不归位、不回退**；因此 Phase 0 的
   起点状态 = `acc5e7b` + 当前在飞改动，重写在其上叠加。
   - **基线落法（已定 a：提交 WIP/前置提交）**：把在飞改动提交到本分支（内容不变；按功能可拆 2–3 个
     提交，或一个 checkpoint 提交），该提交记为 **`B0`**。用 `git worktree add <tmp> B0` 在干净检出上
     `make -C core ghostlock` 构建**基线二进制**，记录 `git rev-parse B0` + 二进制 sha256。现有
     `build/native/ghostlock`（10-02，dirty 树产物）不作基线。
   - **顺序**：`B0`（含在飞改动）→ 建基线二进制 → 修 `cmp_disasm` 的 stale `TARGETS`（纯工具改动，
     二进制不变）→ Phase 0 实现 → 门禁对比。
   - **并发**：`B0` 之后在飞工作若继续改同一批文件（`ancillary_*`、`component_catalog.hpp`、
     `backend_contract.hpp`、`Makefile`、`profile/model.*`、`binary.*`、`README.md`、`AGENTS.md`、
     `adding-a-component.md`），会产生 rebase/冲突，但**不再使基线失效**（`B0` 已不可变）；建议先把在飞
     工作推到稳定点再开 Phase 0，减少重排。
   - **回滚点 = `B0`**（含在飞改动），而非裸 `acc5e7b`；回滚不得丢在飞改动。
2. **`cmp_disasm` 现役 TARGETS 已 stale（门禁天然 FAIL）**。用现有二进制自比：
   `multicast_owner_worker`/`multicast_waiter_worker` 均 `MISSING`，`RESULT: FAIL`；源代码中这两个
   函数已不存在（resident multicast 移除，现为 `route::do_kernel5_fake_lock_route`）。**Phase 0 必须先修
   `TARGETS`（删除/替换 stale 条目）再建新基线**，否则无论实现多正确都过不了门禁。实际攻击函数为 6 个。
3. **`core/CMakeLists.txt` 仅用于 CLion 自动补全（用户确认），不是构建面**。权威构建面是 `core/Makefile`
   （Gradle native 任务 `commandLine("make", "ghostlock")`）。它以 `SOURCES` 给 IDE 建索引，目前缺
   `route_middleware.cpp`/`ancillary/*`/`profile/binary.cpp`/`root_child_frontend.cpp`/`backend/*`/
   `run_state.cpp`。处理：**保留，并随 Phase 0 新增/改名文件同步 `SOURCES`**（加
   `core/session/core_session.cpp`、`core/session/backend/cve_2026_43499_state.cpp`），只为 IDE 索引准确；
   **不进门禁、不作为“构建成功”的证据**。
4. **并行计划重叠需排序**：`docs/archive/20261007-2237-flexible-kernel-rw-primitive-plan.md` 是 Phase C（`KernelMemory`）
   的已细化版本，且明确“值走 session、不给 `attack_write` 加参数”以保 disasm；`5x-tcp-geometry-plan.md`
   改 profile 字段与 TCP 路由；`placeholder-backend-survey.md` 对应占位 backend。顶层计划未给排序，
   存在同一改动两处设计与先后冲突风险。
5. **`session_layout_test` 细节**：`offsetof` 会触发 `-Winvalid-offsetof`（本项目要求零告警），需在测试内
   局部 `#pragma clang diagnostic ignored`；测试可 header-only（只 `sizeof`/`offsetof`，不构造、不链接），
   若构造则需链 `heap_context`/`victim_context`/`native_resource`/`run_state` 等。
6. **构建面清单补齐**：`HOST_ATTACK_DATAFLOW_SOURCES` 必须新增
   `core/session/backend/cve_2026_43499_state.cpp`；`NATIVE_HOST_TESTS` 注册 `session_layout_test`；
   `HDRS` 加两个新头；`CXX_SRCS` 加两个新 `.cpp`（`core_session.cpp`、`cve_2026_43499_state.cpp`）。
7. **文档已在 dirty 集合中被改**：`AGENTS.md`、`core/src/README.md`、`docs/development/adding-a-component.md`
   等已改动；Phase 0 的文档同步应在归位后的新基线上做，避免与在飞改动冲突。
8. **上位计划 Phase 0 描述与 ADR-0002 矛盾**（原写 CoreSession 含 `profile`/`addresses`），已改。
- 2026-10-28：CVE-2026-98260 backend L 计划（含 delta UserService+JNI）：design-critic 三轮正式+三轮增量评审，最终总判「可进批准 gate（阻断 0）」；用户批准（含 debug-only profile 强制确认、device-gate-pending）。B1–B3 离线门禁绿，真机 G0–G13 pending。记录：`docs/analysis/98260-b{1,2,3}-*20261028.md`。
- 2026-10-28：结构适配计划 v1 评审=不可进（阻断 S-01/S-02）；v2 复评=**可进实现（阻断 0）**，R2 移出批准位；v2.1 仅修正指针表述/加残留关闭表（R-01～R-12）。用户批准 R1a→R1c→R1e→R1b+R1d。
- 2026-10-28：98260 结构适配 R1a–R1d 离线实现完成；用户裁定「payload 由公用 victim 启动、finish 由 rooted victim 执行」已落实；离线 native/compile/assemble 绿、聚焦 98260 测试绿；全量 300/7（7=既有 finish 5 + 在途未跟踪 43499 write-chain 2）；真机 G8–G14 pending；未 commit。
- 2026-10-28：结构修正（用户裁定）——删除 98260 专用 Shizuku UserService/AIDL，改为全 backend 公用 `GhostlockUserService` + `ShizukuBackendHost(s)` + `ShizukuBackendClient`；98260 仅保留 host/端口适配。AIDL 编译绿；Kotlin 编译待外部在途 43499 写入停止后复跑。
- 2026-10-28：执行体口径（用户裁定）——攻击进程二选一：App→43499/43284，Shizuku→98260；不迁移 43499/不跳 W3；新增 `BackendExecutionDomain` + readiness/dispatcher 硬门禁。
- 2026-10-28：BackendSpec 动态化（用户裁定）——`Backends` 可观察注册表 + 稳定 `key` 选择 + runner 改 key + 每 spec/Lua 声明 uid 需求；新增 `DynamicBackendsTest`；攻击线程/Flow 迁 host JVM 仍待 R2。
- 2026-10-28：Flow 上一级改为 `FlowLaunchParent`：执行体选择（App=新 JVM 线程；Shizuku=host JVM 待 R2）+ 传入指向选定 Flow 的 lambda（`FlowLaunchRequest`）；Shizuku 未接 host Flow 前具名 pending，不静默回退。
- 2026-10-28：执行体调用统一为 symbol：`FlowSymbolInvocation(symbol+config)` 替换 lambda；`FlowLaunchExecutor` 多态（App=LocalThreadFlowExecutor+注册表解析；Shizuku=ShizukuHostFlowExecutor 同一 symbol）；`FlowLaunchParent.executorFor` 分发；测试改为 symbol 契约。

- 2026-10-29: Exec Glue rev10 plan review (frozen SHA-256 301b1117e371fdb3ff18f8edda0aefda82e2f9434ac1583096a22e1ddb2bb7a7, 15,947 B) = BLOCKED (3 blockers: module-param/UMH ABI+KMI artifacts; AF_UNIX/SELinux/App-Shizuku reachability; HandoffRequested UI/Flow propagation; 2 important: exp cleanup ownership; execve-failure/KO provenance). Finish no-return approved as a design semantic only. User authorizes minimal 43284 C/KO root-handoff changes; implementation remains unapproved.
- 2026-10-29 21:49 UTC: Exec Glue rev11 计划复核（冻结 SHA-256 1dc5ad14e0a478431db3781c48eb5a7587ccc97d49861ba066ace0deab6f251a，15,208 B）= BLOCKED（3 阻断：init↔App/Shizuku runDir 的 DAC/SELinux 可达性、module-param/insmod/KO/UMH ABI 与 KMI 产物、marker 跨 UID 身份/重放；3 重要：exp.c cleanup 结果、HandoffRequested 全链路、FD/exec 失败验证；1 建议：marker schema 纳入 manifest/golden）。Finish no-return 与状态时序一致；该评审不批准生产实现。
- 2026-10-29 21:52 UTC: Exec Glue rev11a 计划复核（冻结 SHA-256 61912b4313150869ef4a3aa0e1a23a2d1a475618409226693bb40cdd80b99925，15,869 B）= BLOCKED（1 阻断：init domain 对 App-private/Shizuku runDir 的 DAC/SELinux 实际可达性；3 重要：逐 KMI ABI/KO hash、exp.c cleanup 返回值、HandoffRequested 全链路；1 建议：root-owned 脚本 FD inheritance/exec fixture）。ROOT_READY/RELEASE_FINISH 防重放与 Finish no-return 合同已收敛；未批准生产实现。
- 2026-10-29 22:05 UTC: rev11a M0 marker-only gate design review（冻结计划 SHA-256 61912b4313150869ef4a3aa0e1a23a2d1a475618409226693bb40cdd80b99925）= BLOCKED（2 阻断：生产 Flow/ELF/KO 当前无 marker-only handoff；缺隔离 fixture 方案与该批精确批准；3 重要：双 launcher runDir 身份/DAC/SELinux、marker replay/替换竞态测试、同批清理与恢复证据）。本轮仅设备预检，Payload/Finish 未运行；要求将 M0 独立批次写入活动计划并复核。
- 2026-10-29 22:08 UTC: Exec Glue rev11b M0 设计复核（冻结草案 SHA-256 87e5c36b6dcc28831e816a91d3cca68cce8b6502e723e295a70f803547ef8940，16,237 B）= BLOCKED（2 阻断：计划超过 A3 16,000 B 配额；A301SO init↔App/Shizuku DAC/SELinux 与 marker 消费仍未实测；2 重要：fixture 文件/hash/安装清理命令需固定、fixture ABI 与生产 KMI 门分离；1 建议：删除前确认 worker/FD 已退出）。隔离 fixture 可作为 M0；不批准生产实现。
- 2026-10-29 22:11 UTC: Exec Glue rev11b 紧缩版 M0 计划复核（冻结 SHA-256 1b232874a756463ab529c34161bdbac37314a2a23fe06571fd9ac52960fa2ff0，15,997 B）= M0 可进入用户已批准的 fixture 实现批次（计划阻断 0；重要 1：实现前固定 fixture 清单/hash/安装清理命令/absence 输出；建议 1：删前核 worker/FD 退出）。实证门未关闭；不批准生产实现。
- 2026-10-29 22:11 UTC: rev11b 复核证据更正：此前把 legacy `cve43284/flow/Flow43284Impl.kt` 当作 Glued production Flow 不准确；实际 `backend/43284glued/Flow43284Glued.kt` 为单 exec step，调用 `ExecutableBackendProcess`，Shizuku host 在 `Glued43284ExecutableHost.kt`。修正后 M0 判决不变：仍需隔离 fixture 实测 init↔双 runDir marker/DAC/SELinux。
- 2026-10-29 22:12 UTC: 43284 M0 review evidence addendum: `Flow43284Glued.kt` is wired as one `ExecutableBackendProcess` step and `Glued43284ExecutableHost` supplies Shizuku runtime arguments; the earlier reference to legacy `Flow43284Impl` as the Glued route is corrected. M0 still requires an isolated marker fixture; no change to the production gate verdict.
- 2026-10-29 22:28 UTC: A301SO 43284 M0 fixture manifest复核（HEAD b119085915748a940740d7ce919b5a30ed8e9346；计划 SHA-256 a3c5afec3ad079f34aabc9959bd0e7e8760ec7efdf6eb0733bbc2c7b12ffa4d5；manifest SHA-256 16fea317a6144822af04491b27f4484a438547175436a71da536e2e68b1bda7a）= M0 可进入 marker-only 设备门；前次 APK 文件哈希/证书指纹歧义已修正；阻断 0，重要 2（默认路径 absence 与双 launcher 设备证据属执行门）。未批准生产实现、Payload/Finish 或 exploit support。
- 2026-10-29 22:50 UTC: A301SO 43284 M0 marker gate实证复核（冻结 HEAD b119085915748a940740d7ce919b5a30ed8e9346；KO `vermagic=5.15.202-android13-5.15.202_r00-dirty` vs device `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）= BLOCKED（1 阻断：M0 KO 与目标 KMI 不匹配；1 重要：不安装、不运行、保持设备 APK 原状）。要求精确 KMI DDK/KO 后重跑；无 marker 协议结论、无生产实现批准。
- 2026-10-30 02:51 UTC: M0 carrier-tail preflight 设计复核（冻结 HEAD b119085915748a940740d7ce919b5a30ed8e9346；`exp.c` SHA `cac62e5e…`；plan SHA `3d83f654…`）= 可进入 M0 fixture-only 实现门（阻断 0，重要 3，建议 1）。要求固定 carrier/offset、单次精确 16-byte 只读尾块、fingerprint/reason code、维持 `patch_helper()` 前顺序；不批准生产实现。
- 2026-10-30 03:00 UTC: M0 source/KO staging 设计复核（冻结 HEAD b119085915748a940740d7ce919b5a30ed8e9346；APK 仍含旧 `carrier-stat`）= BLOCKED（阻断 2：`-x buildM0MarkerKo` 同时跳过 source staging，generic KO/source output 重叠；重要 2：source+KO 双身份与 Gradle DAG 未锁定；建议 1：更新 plan/manifest）。必须分离 `stageM0MarkerSources`、verified KO 输入与 generic builder output，增加 source manifest/ELF string/embedded-KO hash gates 后方可重试；无生产实现批准。
- 2026-10-30 03:05 UTC: M0 source/verified-KO staging implementation 复核（冻结 HEAD b119085915748a940740d7ce919b5a30ed8e9346；final ELF SHA ce7789c73d60414e1031a33a2eedc1e6187d18594ae43ee42d59004f02fb53fb；APK SHA bf125562352933b6e5e9e257ee4d0b46be6e97574e2b882ee42c8999aa2dbc58）= 可进入本批唯一一次 A301SO App marker retry（阻断 0，重要 2，建议 1）。Source/KO/ELF/APK identity、task DAG 与 JUnit `()V` 已核；仍未验证真实 tail read、ROOT_READY/RELEASE_FINISH、worker cleanup/absence。未批准生产实现、Shizuku、Payload/Finish 或 production Flow。
- 2026-11-02 19:16 UTC: Flow 进程启动器 L 计划（新架构：攻击一律新进程 + reflection symbol 仅 Flow 函数）三轮只读复核（冻结 HEAD b119085915748a940740d7ce919b5a30ed8e9346；v3 `ea9ca2d2…` 通过 = 可进用户批准 gate（阻断 0，重要 3）；v4 `a8dc0035…` 仅修 3 项重要）。用户 goal「实现新进程攻击机制」= 批准按 D1–D7 推荐值实现；计划 v5 生效。未跑构建/真机。
- 2026-11-02 19:22 UTC: B0/B1a 完成。B0：计划 v5 生效，执行通道取代指针同批落入 exec-glue-plan/execution-architecture/98260-user-requirements-summary/MASTER-PLAN（各文件头部）。B1a：8 个新文件（FlowSymbol/FlowEntry/FlowEntryRegistry/FlowHostParams + 4 测试），`:app:testDebugUnitTest --tests "com.ghostlock.app.core.flow.*"` EXIT=0（42 tests/0 failures/0 errors；Lead 独立复跑）、`:app:compileDebugKotlin` EXIT=0、`git diff --check` EXIT=0；无既有文件改动/无 commit。偏差：Robolectric 4.17 在 JDK25 崩溃 → Bundle wire + 持久化 schema/migration 并入 B2（UNVERIFIED）；bridge/synthetic reason 受“同名唯一”前置顺序限制，用等价负例留证。B2 起实现 `:flowhost`/AIDL/launcher/HandoffRequested。
- 2026-11-02 19:30 UTC: B2a/B2b 完成。B2a：`:flowhost` Service + IFlowHost AIDL + AppProcessFlowLauncher + `BackendRunUiState.HandoffRequested` + 诊断 fixture（FlowHostProtocolTest 9/0/0；compileDebugKotlin/assembleDebug/assembleDebugAndroidTest/`git diff --check` 全绿；`RunPage.kt` 两处 sealed 穷举为计划 §4 列出的 B7 文件，属验收内编辑）。B2b（A301SO `QV770MFGJ1`，Android15/5.15.189/Enforcing/RUNNING_UNLOCKED；debug APK sha256 38649ed7…；test APK 2d9db003…）：`am instrument -w -e class com.ghostlock.app.backend.core.FlowHostProcessSmokeTest` 两次均 `OK (4 tests)`（7.487s/7.34s）；覆盖 Finished.Ok+3 行 diag、每次 run 新 pid 且旧进程消失、非法 symbol 具名拒绝、payload 超限拒绝、handoff→HandoffRequested 不报成功；logcat 见 `:flowhost` pid 28185→28202 spawn→died、`flow-symbol:unregistered`。报告 `docs/analysis/device-gates/20261102-flowhost-a301so-smoke.md`。UNVERIFIED：cancel/task 移除/bind 超时/同 UID 复用显式注入（B2c）、UI 集成（B4a）、Shizuku flavor（B3b；A301SO 无 Shizuku 包）。
- 2026-11-02 19:36 UTC: B3a 完成（Shizuku 进程 FlowHost）。5 文件：`IGhostlockUserService.aidl` 追加 flow API（显式 id 12–18；旧 1..11 与 destroy=16777114 不变）+ `GhostlockUserService` flow 分支（IDLE→PREPARED→RUNNING→TERMINAL；verifyFlow 追加 host_uid/host_pid/host_domain；旧 backend host 方法保留至 B5c）+ `ShizukuFlowHostClient`/`ShizukuProcessFlowLauncher` + androidTest smoke（assumeTrue）。Lead 独立复跑：`:app:compileDebugKotlin` EXIT=0；定向单测 **51 tests/0/0**（Protocol 9 + core.flow 42）EXIT=0；`assembleDebug+assembleDebugAndroidTest` EXIT=0；`git diff --check` EXIT=0。UNVERIFIED：B3b 设备门禁（A301SO 无 `moe.shizuku.privileged.api`）：真机 uid2000/shell domain、每次 bind 新进程、Context=null、取消；B4a 生产 Flow 接入待做。
- 2026-11-02 19:42 UTC: B4a 完成（43499-Glued 迁入新进程通道）。15 文件/2685 行：host.* 注入参数（app/shell run_dir 0700+exists/symlink 校验+cleanup evidence）、`Flow43499GluedEntry` context-free 入口（staged profile→exec lib43499-Glued.so→有界 waitFor/并发 stdio→evidence）、`BackendSpec.flowSymbol/flowParams`、dispatcher 分流（43499 runner=null 单权威；其它 backend 旧 runner）、design-critic I1–I5 已并入。Lead 复跑：定向单测 **63 tests/0/0** EXIT=0；compileDebugKotlin/assembleDebug/assembleDebugAndroidTest EXIT=0；`git diff --check` EXIT=0。B4a-b 设备门禁（A301SO）：fixture no-op ELF APK sha `a7305ab8…`（生产 APK `b51c7579…`）；`Flow43499GluedHostSmokeTest` 两次 **OK (1 test)**，覆盖 gate allowed/Executed/Finished.Ok/fixture marker/host_pid≠app/两次新 host pid/host_run_dir；恢复生产 APK 后 `FlowHostProcessSmokeTest` **OK (4 tests)**。报告 `docs/analysis/device-gates/20261102-b4a-43499-flowhost-fixture.md`。UNVERIFIED：B4-real 真实漏洞冷机门禁待用户授权；43284/98260/Lua 迁移（计划 B4b/B5）；Shizuku flavor（B3b 设备无 Shizuku）；超时/取消实测。
- 2026-11-02 19:50 UTC: B4b 完成（43284Glued 迁入新进程通道）。10 文件/2167 行：`FlowHostEnv.context`（host-local；B4b 例外，43499 不读）、host 注入 Context（app Service / Shizuku `userServiceContext` nullable）、共享 `HostProcessExec`（43499 仅重构、A11 有界等待语义不变）、`Flow43284GluedEntry`（IpSec runtime 生命周期：normal close/close-failed；超时/取消→`runtime-leaked`+`host_keepalive`、host 跳过自杀与清理）、`Backend43284Glued.flowSymbol`+`runner=null`、两 host allowlist、`BackendFlowSelectionTest`/`HostProcessExecTest`。design-critic task-148 唯一阻断（B4a 附录仍写不增加 Context）已同批由 `annex-b4a.md:21` 加 B4b 例外指针闭合。Lead 复跑：定向单测 **70 tests/0/0** EXIT=0；compileDebugKotlin/assembleDebug/assembleDebugAndroidTest EXIT=0；`git diff --check` EXIT=0。B4b-b 设备门禁（A301SO；生产 APK `1999bc52…`、fixture APK `385d4fa5…`、test APK `e0adf4a0…`）：`Flow43284GluedHostSmokeTest` 两次 **OK (1 test)**、`Flow43499GluedHostSmokeTest` 回归 **OK (1 test)**；logcat 证 `:flowhost` 内真实 IpSec SPI/Transform create/remove；恢复生产 APK 后 `FlowHostProcessSmokeTest` **OK (4 tests)**；无 `:flowhost` 残留。报告 `docs/analysis/device-gates/20261102-b4b-43284-flowhost-fixture.md`。UNVERIFIED：B5 真实 43284 攻击/keepalive 设备路径；B3b Shizuku flavor；超时/取消/大 stdio。
- 2026-11-02 19:55 UTC: G9 完成（Host allowlist 改由 BackendSpec 生成 registry）。5 文件/1272 行：`FlowEntryRegistryAssembly`（productionSymbols=`Backends.all.flowSymbol`；productionRegistry；hostRegistry 仅 DEBUG 追加诊断 fixture）+ `DiagnosticFlowEntries.symbols` + 两个 host 的 `builtInSymbols()/flowBuiltInSymbols()` 删除并改用 assembly（失败具名 `flow-host:registry:*`/`flow-symbol:*`）；新增 `FlowEntryRegistryAssemblyTest`（production/union/双向一致/未知/重复 fail-closed）。Lead 复跑：定向单测 **65 tests/0/0** EXIT=0；compile/assembleDebug/assembleDebugAndroidTest EXIT=0；`git diff --check` EXIT=0。设备回归（A301SO；生产 APK `ce4eace2…`、fixture APK `227c91f4…`、test APK `e0adf4a0…`）：生产 APK `FlowHostProcessSmokeTest` OK(4)（debug hostRegistry 放行诊断）；fixture APK `Flow43499GluedHostSmokeTest` OK(1)、`Flow43284GluedHostSmokeTest` OK(1)（生产符号经 registry 解析）；恢复生产 APK 后 B2 OK(4)；无 `:flowhost` 残留。报告 `docs/analysis/device-gates/20261102-g9-registry-a301so.md`。UNVERIFIED：Shizuku shell 进程 registry/Compose 初始化（B3b）；release/R8 分支；Lua 动态注册（B5）；B4-real 真实攻击门待授权。
- 2026-11-02 20:00 UTC: B5c-1 + release 编译修复完成。删除旧 in-process runner/双执行体通道（`BackendFlowRunner`、`FlowLaunchParent` 旧执行器、`BackendSpec.runner/appOwnedFlowWithAutomaticProcessLauncher`、6 spec runner override；dispatcher 只剩 `launchSymbol`）；仓外备份 `/tmp/ghostlock-b5c1-backup-20261102T195528Z`（MANIFEST sha `a833dab0…`，19 文件）。release 修复 `defaultConfig M0_MARKER=false` → `:app:assembleRelease` EXIT=0（release APK sha `4b3f6407…`）。Lead 复跑：全量 `testDebugUnitTest` BUILD SUCCESSFUL EXIT=0（agent XML 486/0/0/0）；debug/androidTest/release 构建 EXIT=0；`git diff --check` EXIT=0。设备回归（A301SO；debug APK `b64237c4…`、fixture APK `31c21fb4…`、test APK `e0adf4a0…`）：生产 APK `FlowHostProcessSmokeTest` **OK(4)** 7.48s；fixture `Flow43499GluedHostSmokeTest` **OK(1)** 4.325s、`Flow43284GluedHostSmokeTest` **OK(1)** 4.118s；恢复生产 APK 后 B2 **OK(4)** 7.412s；无 `:flowhost` 残留。报告 `docs/analysis/device-gates/20261102-b5c1-postchannel-a301so.md`。UNVERIFIED：release APK 未安装/运行；gate Shizuku 未授权设备用例（A301SO 无 Shizuku）；B5c-2 旧 exec/Shizuku host 死代码；B4-real 待授权；Lua NO_FLOW_RUN。
- 2026-11-02 20:10 UTC: B5c-2 完成（删除 43499/43284 旧 ELF exec 通道死代码）。19 删除文件（main 10：ExecutableBackendProcess/ExecutableShizukuHost/ExecutableRunFileStager/ExecutableRunEnvironment/ExecutableHostProtocol/Utf8LogLineAssembler/Glued*ExecutableHost/旧 Flow43499Glued/Flow43284Glued；unit 5；androidTest 4），`ShizukuBackendHost` 仅移除 Glued wire id（98260 host/AIDL 保留）；仓外备份 `/tmp/ghostlock-b5c2-backup-20261102T200303Z`（MANIFEST sha `8ae0a112…`）。Lead 串行门禁（clean 后单 Gradle 调用）：全量单测 **470/0/0/0** EXIT=0；debug+androidTest+release 构建 EXIT=0（release APK sha `2fa9fc68…`）；`git diff --check` EXIT=0。设备回归（A301SO；debug APK `17a41937…`、fixture APK `b9af596e…`、test APK `e7bae11c…`）：B2 生产 APK **OK(4)** 7.624s；fixture B4a **OK(1)** 4.362s、B4b **OK(1)** 4.056s；恢复生产 APK B2 **OK(4)** 7.430s；无 `:flowhost` 残留。报告 `docs/analysis/device-gates/20261102-b5c2-postchannel-a301so.md`。附注：Lead 首次并行跑 test+assemble 造成共享 build 目录竞争（两任务 FAILED），clean+串行后全绿；并发门禁属流程错误，已记录。UNVERIFIED：release APK 未运行；98260 legacy Shizuku 通道未设备回归；ExecutableProcessRouting 死文件候选 B5c-3；B4-real 真实攻击门待用户授权（annex-b4-real 已出设计）。
- 2026-11-02 20:20 UTC: 收口第 8 轮：UML 更新 + release 启动 smoke。`docs/development/full-process-uml.md` §11.7 标历史并新增 §11.9「新进程 Flow 通道」（flowchart + host 注入参数/43499 context-free/43284 context/Ipsec 生命周期/新进程一次一 run/registry debug-only 诊断/已交付证据与 UNVERIFIED）；release APK（sha `2fa9fc68…`）在 A301SO 安装并启动 MainActivity（`Displayed +1s127ms`，pid 4002，无 FATAL），随后恢复 debug+test APK；报告 `docs/analysis/device-gates/20261102-release-smoke-a301so.md`。`git diff --check` 后续复核。UNVERIFIED：release 下 :flowhost/攻击流程未执行；R8 反射裁剪未验证；B4-real 真实漏洞冷机门禁待用户授权；98260 legacy host/AIDL 与 Lua 动态 symbol 注册为 B5c-3/B5a/B5b 遗留。
- 2026-11-02 20:30 UTC: G10 完成（launcher 取消语义 + 设备取消门禁）。改动 6 文件：`AppProcessFlowLauncher`（ensureActive+delay；CancellationException→host.cancel→rethrow）、`ShizukuFlowHostClient`/`ShizukuProcessFlowLauncher`（可取消+cancelFlow+pid 回写）、`DiagnosticFlowEntries.DiagnosticSlowFlow`（20s tick/`diag:cancelled`，debug registry）、新 `FlowHostCancellationTest`（等首 tick 后取消；Shizuku preflight uid/domain skip）、G9 测试期望列表邻接。仓外备份 `/tmp/ghostlock-g10-backup-20261102T201043Z`（MANIFEST sha `01c65b1b…`）。设备（A301SO；debug APK `4bc88119…`、test APK `6098bddc…`、fixture APK `d21c5779…`）：`FlowHostCancellationTest` **OK(2)**（App 取消 PASS；Shizuku 按 uid/domain skip）；B2 **OK(4)** 7.373s；fixture B4a **OK(1)** 4.381s、B4b **OK(1)** 4.150s；恢复生产 APK 后 B2 **OK(4)**；无 `:flowhost` 残留。报告 `docs/analysis/device-gates/20261102-g10-cancel-a301so.md`。UNVERIFIED：真实 Shizuku shell 取消（B3b）；A11 完整语义（真实 payload 取消/keepalive）未测；`bindService` 10s 阻塞等待期间取消仍不即时；B4-real 待授权。
- 2026-11-02 20:40 UTC: G11 完成（A11 keepalive 机制设备门禁）。3 文件/439 行：`DiagnosticKeepaliveFlow`（Failed diag:keepalive + `host_keepalive=true`，debug registry）、G9 测试期望同步、新 `FlowHostKeepaliveTest`（断言 Finished(Failed)/evidence/`host_run_dir_cleanup=skipped`/host pid 存活；测试内 kill -9 回收 + 新 run 新 pid）。备份 `/tmp/ghostlock-g11-backup-20261102T201837Z`（MANIFEST sha `f75a0274…`）。设备（A301SO；debug APK `541860f5…`、test APK `9062ac74…`、fixture APK `1dbcf48f…`）：`FlowHostKeepaliveTest` **OK(1)** 3.862s；A11 残留 run_dir `run-31zig14nc` 可观察（cleanup=skipped）后 pm clear；B2 **OK(4)** 7.380s；fixture B4a **OK(1)**、B4b **OK(1)**；恢复生产 APK 后 B2 **OK(4)**；无 `:flowhost` 残留。报告 `docs/analysis/device-gates/20261102-g11-keepalive-a301so.md`。UNVERIFIED：真实 43284 超时/取消→runtime-leaked 生产路径未设备触发；真实载荷在 keepalive 期间的 A11 语义未验证；Shizuku keepalive（B3b）；keepalive host/run_dir 无自动回收 watchdog（技术债）；B4-real 待授权。
- 2026-11-02 21:00 UTC: G12 完成（43284 生产 entry 超时→runtime-leaked+keepalive+A11 未 kill 载荷真机门禁）。仅新增 androidTest `Flow43284TimeoutKeepaliveTest`（240 行；slow fixture marker 才执行）：`AppProcessFlowLauncher` 直接调 43284 symbol、`exec.timeout_ms=2000`，断言 Failed(`flow-entry:43284:timeout`)/`ipsec=runtime-leaked`/`host_keepalive=true`/`host_run_dir_cleanup=skipped`/host pid 存活，扫描 `/proc/*/cmdline` 证 payload 未 kill，测试后 kill host 并等 payload 自然退出，fresh Ok flow 新 pid。Lead 构建 slow fixture APK（sha `0319635c…`；43284=shell sleep6 脚本、43499=no-op）与 normal fixture（`10e9c3df…`）。设备（A301SO；debug APK `b0eba4ab…`、test APK `043c3566…`）：slow fixture 测试 **OK(1)** 5.860s；B2 **OK(4)** 7.513s；normal fixture B4a/B4b **OK(1)/OK(1)**；恢复生产 APK B2 **OK(4)**；无 flowhost/payload 残留。报告 `docs/analysis/device-gates/20261102-g12-timeout-leak-a301so.md`。UNVERIFIED：真实 43284 漏洞 ELF 超时语义（slow fixture 为脚本）；Shizuku keepalive/超时（B3b）；keepalive watchdog 技术债；B4-real 待授权。
- 2026-11-02 21:30 UTC: G13 完成（Shizuku keepalive unbind 修复 + App/Shizuku 绑定期可取消）。2 文件/494 行：`AppProcessFlowLauncher`（CountDownLatch→suspend `waitBound` 25ms+ensureActive，上限 10s；取消立即抛出）、`ShizukuFlowHostClient`（绑定等待同样 suspend；绑定期取消先 unbind 再 rethrow；terminal `host_keepalive=true` ⇒ `unbindUserService(..., remove=false)` 保留 shell 进程，非 keepalive remove=true）。备份 `/tmp/ghostlock-g13-backup-20261102T211928Z`（MANIFEST sha `9ddd5006…`）。Lead 串行门禁：全量单测 **470/0/0/0**、debug/androidTest 构建 EXIT=0、`git diff --check` EXIT=0。设备（A301SO；debug APK `96a3606d…`、test APK `043c3566…`、slow fixture `8db55d88…`、normal fixture `bc964449…`）：`FlowHostCancellationTest` **OK(2)** 8.312s、`FlowHostKeepaliveTest` **OK(1)** 3.916s、B2 **OK(4)** 7.641s；slow fixture `Flow43284TimeoutKeepaliveTest` **OK(1)** 5.918s（非 skip）+ B2 OK(4)；normal fixture B4a/B4b **OK(1)/OK(1)**；恢复生产 APK B2 **OK(4)**；无 flowhost/payload 残留。报告 `docs/analysis/device-gates/20261102-g13-bind-keepalive-a301so.md`。UNVERIFIED：Shizuku keepalive/unbind(remove=false) 真机（B3b，无 Shizuku 包）；绑定窗口内取消未单独计时（代码/编译证据）；Shizuku tag 缓存竞态；keepalive watchdog 技术债；真实 43284/B4-real 待授权。
- 2026-11-02 21:45 UTC: B5c-3 收口（机制层完成后的范围/遗留/回收策略归档）。新增 `docs/plan/flow-process-launcher-annex-b5-closeout.md`（迁移范围：exec-glue build 可选仅 43499-Glued/43284Glued；B5a 98260/B5b Lua 因不可选不适用，重新启用时需补 flowSymbol/entry/registry；B5c-1/2 已删旧 runner/双执行体与 43499/43284 旧 exec 通道；B5c-3 残留=98260 legacy Shizuku host/AIDL host 方法+Lua 动态 symbol 注册 gap；A11 keepalive 当前=有意保留无 watchdog，回收=重启/pm clear/人工，候选策略未实现；未验证总表含 B4-real/B3b/release/98260 等）。主计划 v5 同步：§5 B4b/B5a/B5b/B5c 状态、§8 收口、D6 实际状态；`exec-glue-plan.md` 指针补“旧 exec 通道已删/可选 backend 仅两个 Glued”。plan 16,225 B（≤16 KiB）；`git diff --check` EXIT=0。UNVERIFIED：B4-real 真实漏洞冷机门禁待用户授权；B3b Shizuku 环境；release 攻击流程；keepalive watchdog；98260/Lua 迁移技术债。
- 2026-11-02 21:50 UTC: B4-real 首次真实 43499 尝试（用户「允许授权」后，A301SO 冷机、opt-in `Real43499GateTest`、生产 `:flowhost`+真实 ELF）：运行 >7min 无 `GLK-REAL43499`/host 证据，被 Lead 中止并 reboot；设备未 panic/未 root、无残留；未归档为 PASS/FAIL，记为“未完成、方向待修”。用户随后裁定：根因是 43499 上游配置系统 C++/Kotlin 复杂度处理不当，要求移植上游流程「加载磁盘配置→validate→添加 execution params→传入 C++→解析成 document」。新增设计 `docs/plan/flow-process-launcher-annex-config-document-port.md`（C1–C5：C1 native mpack/glkv3 document 读取+golden、C2 Kotlin Glkv3 encoder/merge、C3 切生产入口+fixture 设备门禁、C4 真实重试、C5 删 GLK1 双轨；附参考取证 `2933b838^`/`d2778a1a^`、风险/开放决策）；主计划 §8 记录；已发 design-critic task-160（G15）复核。UNVERIFIED：C1–C4 全部未实现；B4-real 未完成；范围（最小 vs profile-core 全量）与 msgpack 依赖待用户/critic 决定。
- 2026-11-02 22:00 UTC: 附录 C v2（配置/document 管线移植）按 G15 修订：B1 改为前置映射文档 `docs/analysis/43499-document-key-mapping.md`（只读分析 subagent 在办；C1 写码前须完成并验收）；I1 裁决 C1–C3 最小集不含 plan_gate/run_plan/plan_delivery/route_catalog，C4 前不恢复，需则另批 C4a；I2 修正 C1 实际测试集（`document_reader_test` 存在，critic 取证遗漏；plan_gate_test 不在 C1）；I3 裁决 `org.msgpack:msgpack-core:0.9.12`（上游版/纯 Java/Apache-2.0）+ mpack MIT vendored（保留 LICENSE/README）；I4 C2a Resolver/Merger 行为差异表；I5 补估算/单 writer/C3 只切、C5 删/备份；I6 最小范围排除 plugin/route/component/Lua/M0、APK 增量≤300KiB、P4 旧文档 superseded 指针。annex C v2=8,294 B/sha256 `4a99baae…`；主计划 §8 已指 `annex-config-document-port`。UNVERIFIED：映射文档未交付；C1–C5 未实现；critic v2 复核未跑；B4-real 未完成。
- 2026-11-02 22:30 UTC: 用户手动删除 98260 后收口 + 新增 Debug Probe backend。98260 收口：删除 main 5（data/cve98260、Cve98260Preflight、Native98260Jni）/test 8（cve98260 包、BackendCve202698260Test、FolkPatchKoTest 等）、修改 main 13/test 5（BackendId/BackendSpec/GhostLockApp/RunPage/Shizuku host-client/strings 等；app/src 内 `98260` 仅剩 2 处注释）；备份 `/tmp/ghostlock-98260-probe-cleanup-20261102T222142Z`（31 文件，MANIFEST `3cd86696…`）。Debug Probe：新增 `backend/debugprobe/{BackendDebugProbe,FlowDebugProbeEntry}.kt`（debug-only selectable/flowSymbol；entry 打印 host.*/Build/System props/env//proc/SELinux/boot_id/uptime + 128 行 marker；持久化到 run_dir 外 `files/ghostlock-glue/flowhost/debug-probe/debug-probe-last.log`，evidence persist_path/persist_bytes），BackendSpec builtins/isSelectable debug 门控，strings×2，测试 Assembly/Selection 更新，新增 `DebugProbeHostSmokeTest`（App 生产 dispatcher + Shizuku preflight 分支）。门禁：全量 `testDebugUnitTest` **421/0/0/0**；debug/androidTest/release 构建 PASS（release APK sha `371a341c…`，release probe 不可选）；`git diff --check` EXIT=0。设备（A301SO）：`DebugProbeHostSmokeTest` **OK(2)**（App 分支 PASS：Finished.Ok/128 行/host_log 清理语义/persist_path 文件含 done+line127；Shizuku 分支 skip）。发现：设备 Shizuku 13.6.0 已安装运行（`moe.shizuku.privileged.api` pid），但 GhostLock 未授权（`ShizukuFlowHostSmokeTest` 4 tests 全 skip；preflight binder/permission/uid/domain 不满足），设备当前锁屏需 PIN。UNVERIFIED：Shizuku probe/B3b 待授权；B4-real H1 修复后冷机重试未跑；release probe 运行时 gating 未跑。
- 2026-11-02 22:45 UTC: 用户 goal「新增 debug probe 后端」完成 + Shizuku B3b 真机验证突破。Debug Probe：debug-only backend（`BackendDebugProbe`；release NotReady/不可选/不在 registry），entry 打印 host.*/Build 扩展/System props+env/`/proc`(version/status/meminfo/cpuinfo/cmdline/attr/current)/SELinux/boot_id/uptime/Locale/Timezone/Runtime memory/CPU online+present/availableProcessors/NetworkInterface + 128 行 marker + done；run_dir 外持久化 `files/.../flowhost/debug-probe/debug-probe-last.log`（0600，evidence persist_path/persist_bytes；run_dir 内 host_log_path 按 cleanup 语义删除）。门禁：全量单测 **421/0/0/0**；debug/androidTest/release 构建 PASS（release probe 不可达）；设备 A301SO：`DebugProbeHostSmokeTest` **OK(2)**（App 分支强制 App executor：2 次 run 新 pid 18882→18900、227 行、persist 含 done/line127、filesDir 断言；**Shizuku 分支 PASS**：shell host pid 18917、uid=2000/`u:r:shell:s0`、243 行、shell persist `/data/local/tmp/ghostlock-flow/...` app 不可读→skip 持久化断言不失败）；`ShizukuFlowHostSmokeTest` **OK(4)**（Shizuku 13.5 adb server 启动后 B3b：新 pid/旧进程消失/handoff/非法 symbol/payload 负例均 PASS）。附注：设备 shizuku 通过 `adb shell <apk>/lib/arm64/libshizuku.so` 启动；GhostLock 已授权（UI 显示已授权；probe 分支 permission=true）。UNVERIFIED：release probe 运行时 gating；Shizuku persist 文件仅 shell 可读（app 侧只能 evidence 路径）；B4-real 真实攻击 H1 修复后冷机重试仍待跑。
- 2026-11-02 22:55 UTC: 后端注册表统一 B1–B4 完成（用户裁定；G17 critic 阻断 0/重要 5）。`BackendSpec` 增必选 `ConfigCard`（默认 `DefaultBackendConfigCard(rows)`；43499Glued/43284原版/Lua 覆盖），删 `BackendId`/`id`/`customConfig`（grep app/src=0），`Backends.isSelectable=spec.selectable`（删对象身份白名单），`register` fail-closed（空/重复 key、selectable 无 flowSymbol 拒绝），release builtins 过滤 debug probe；`BackendPage` Material/Miuix 单一路径 `spec.ConfigCard`；`BackendSessionStore` legacy CVE 前缀抽 `resolveStoredSpec` 纯函数并写回 canonical key。备份 `/tmp/ghostlock-backend-registry-backup-20261102T224155Z`（MANIFEST `73905d50…`）。门禁：全量 **434/0/0/0**；debug/androidTest/release 构建 PASS（release APK sha `00d913ac…`）；`git diff --check`。设备 A301SO debug UI 走查：菜单 6 项（43499-Glued/43284Glued/禁用原版×2/自定义/调试探针），43499-Glued ProfileCard、43284Glued/DEBUG-PROBE 默认 rows 卡渲染正确，禁用项点击不改变选择；release APK 菜单 5 项**无调试探针**；`DebugProbeHostSmokeTest` OK(2)、B2 OK(4) 回归 PASS。UML 新增 §11.9.1（注册表→菜单/ConfigCard/flowSymbol 图）。UNVERIFIED：release 运行 probe；SessionStore 旧 prefs 真机迁移；动态 Lua spec 新卡设备走查。
- 2026-11-02 23:05 UTC: backend 目录整理 M1–M2 完成（goal「整理」round 1）。16 MOVED：`backend/core/` 拆分→host/（5）/launcher/（2）/registry/Backends.kt/diagnostics/DiagnosticFlowEntries；declarations 物理归位（BackendSpec、旧原版 43499/43284 声明、Profiles43499、Lua BackendCustom）；UI 归位（DefaultBackendConfigCard、CustomLuaConfig→ui/component/backend）；package 修正 3（FinishUi/FinishSettingsUi、Cve43499CleanFailureReason）；AndroidManifest FlowHostService FQN→backend.host；删除空目录 core/general；main/test/androidTest imports 全量更新；逻辑/签名/文案零改动（备份 diff 49 文件、0 unexpected 行）。备份 `/tmp/ghostlock-backend-dir-cleanup-backup-20261103T000433Z`（52 文件，MANIFEST `059e9c3d…`）。Lead 复跑：package/dir mismatch **CHECKED=449 MISMATCH=0**；全量单测 **434/0/0/0**；debug/androidTest/release 构建 PASS；`git diff --check`。设备 A301SO：debug UI 菜单 6 项/卡片正常、release 菜单 5 项无 probe；`DebugProbeHostSmokeTest` OK(2)、B2 OK(4)、`ShizukuFlowHostSmokeTest` OK(4)（新 FlowHostService FQN bind smoke 通过）。UNVERIFIED/遗留：测试源码物理目录仍 `backend/core`（package 一致）；空目录 `backend/cve43499/debug/`；M3 active/legacy 分区与 M4 旧代码删除待用户裁定。
- 2026-11-03 00:35 UTC: backend 目录整理 M3–M4 完成（用户裁定：不可选 backend=未完工，不删除）。105 文件移动：`backend/active/{cve43499glued,cve43284glued,debugprobe}`（package `...backend.active.*`）+ `backend/unfinished/{cve43499(47 含子包),cve43284,lua}`（package `...backend.unfinished.*`）+ 测试镜像迁移（test/cve43499→unfinished、test/cve43499glued/cve43284glued→active、androidTest 对应）；`declarations/` 仅留 BackendSpec/BackendRow/BackendSelection + FlowEntryRegistryAssembly；新增 `backend/README.md`（unfinished 保留/不可选/灰显/禁止清理删除）；UML §11.9.1 布局注记、计划 §9/§10 记录。备份 `/tmp/ghostlock-backend-m3m4-backup-20261103T001839Z`（127 文件，MANIFEST `ff298635…`，127/127 校验）。Lead 复跑：mismatch **CHECKED=449 MISMATCH=0**、旧包前缀 **0**、全量单测 **434/0/0/0**、debug/androidTest/release 构建 PASS、`git diff --check`。设备 A301SO：probe OK(2)、B2 OK(4)、Shizuku OK(4)、fixture B4a/B4b OK(1)/OK(1)（新 FQN 反射解析）；debug UI 菜单 6 项/43499 ProfileCard 正常、release 菜单 5 项无 probe；已恢复 debug+test APK。UNVERIFIED/遗留：未 commit（git D+untracked，需 `git add -A`）；test `backend/core/` 未迁 host；docs/analysis 历史旧包名保留。
- 2026-11-03 00:55 UTC: backend 目录统一完成（用户裁定：不区分 active/unfinished，统一 `backend/backends/`，未完工用 `@UnfinishedBackend` 注解）。105 文件移动（active/unfinished→backends；package `...backend.backends.*`；测试镜像 33+7；FlowSymbol FQN/registry imports/KDoc/测试 FQN 全量更新）；新增 `declarations/UnfinishedBackend.kt`（CLASS/RUNTIME/@MustBeDocumented，reason）标注 3 个未完工 spec（原版 43499/43284、Lua BackendCustom；debug probe 不标）；新增 `UnfinishedBackendAnnotationTest`（4 用例，负向证明：临时移除注解→2 失败→恢复 sha 一致）；README/UML/计划更新。备份 `/tmp/ghostlock-backend-unify-backup-20261103T003709Z`（128 文件，MANIFEST `4409bfb0…`，128/128）。门禁：mismatch **CHECKED=451 MISMATCH=0**、旧词 0、全量 **438/0/0/0**、compile+3 assemble PASS、`git diff --check`。设备 A301SO：probe OK(2)、B2 OK(4)、Shizuku OK(4)、fixture B4a/B4b OK(1)/OK(1)（新 FQN 反射解析）；已恢复 debug+test APK。UI 走查本轮未重跑（设备锁屏需 PIN；上轮同 UI 已走查）。UNVERIFIED：未 commit（D+untracked，需 `git add -A`）；UI 本轮未走查。
- 2026-11-03 02:20 UTC: CMake 整理完成（用户裁定：根 CMake 只 include/组装 common，每子文件夹自有 CMake，common 编译为 .o，其余为 .so）。结构：`cpp/{CMakeLists.txt,common/,glgeneral/,backends/{CMakeLists.txt,43499glued,43284glued,43499,43284,98260}}`；根仅 project+3 includes，`backends/CMakeLists.txt` 声明 6 个 option 并按 option include chunk；9 CMakeLists、include_guard、全部 `${CMAKE_CURRENT_LIST_DIR}`；`common` 改 OBJECT `gl_common`（.o 实测）；payload 仍 executable ELF（AGP lib*.so 打包名），DSO SHARED；98260 ON+缺源 FATAL。移动 456 文件（5 目录），删除 ignored cmake-build-debug。common/gl_log.c|h 按裁决未动（2026-10-29 既有日志规范）。Gradle 加 6 个 -D 透传 + M0 路径修复。门禁：全量 438/0/0/0；debug/release APK 各 4 libs；option 矩阵（43499_GLUED=OFF/43284_GLUED=OFF/43499=ON/43284=ON/98260=ON FATAL）逐项；`git diff --check`。设备 A301SO：probe OK(2)/B2 OK(4)/Shizuku OK(4)/fixture B4a/B4b OK(1)/OK(1)（首次 fixture 失败因 androidTest APK 未重建的旧类名，重建后 PASS）。文档 `cmake-backend-chunks`/UML §11.8 更新。UNVERIFIED：未 commit（D+untracked）；M0 未实构建；98260 源缺失。
- 2026-11-03 03:15 UTC: 用户知情强制越过 AGENTS §2 L 计划门并暂准 A301SO compile-time define 参数权威；exact boot/BTF/disasm 取证后 ids/leak 冷机 PASS，real-cred 同 ELF 两次分别为“受控 PFN 命中但 uid 仍 2000”与“reclaim miss 后 park”，按连续失败停止且未运行 fake/KSU；两次均强制重启，最终 Enforcing/KSU_UNLOADED/无 helper 残留。代价=本批仍 device-gate-pending/不得 supported；偿还时机=real-cred 根因单变量 debug 后重过冷机门，再审 fake 门。
- 2026-11-03 03:40 UTC: 98260 A301SO r2 根因关闭：单变量 child uid readback 在同一冷机连续 3 次受控 order-1 PFN 命中后仍为 uid 2000；exact `exec_mmap` 源码/反汇编证明 5.15 使用 non-killable `mmap_read_lock`，而主线 6.1/6.6/6.12 trigger 依赖 `mmap_read_lock_killable` 在 `posix_cpu_timers_exit` 前失败，故不是 offset 问题。参数新增 prerequisite=0/runtime fail-closed，最终真机 ids/base/cred leak 后 exit 2，未 plant，Enforcing/KSU_UNLOADED/无 helper；fake/KSU 未运行，下一变量仅限等价 post-de_thread/pre-cleanup 失败窗口。
