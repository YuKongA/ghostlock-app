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
7. **文档一致性**：`src/core/README.md` 路径、`engineering-standards.md`、`ancillary-controller-guide.md`
   （“布局固定/不得新增字段”将失效）、`host-attack-dataflow-plan.md` 需加现状注；上位计划“`ExploitSession`
   字段布局固定 / 不移动字段”的表述与 Phase 0 矛盾，已改。
8. **上位计划验证矩阵缺 Phase 0 行**：已补。

仍属未知、留待实现期验证：NDK ABI 下 `RuntimeConfig`/`TargetProfile` 的确切 `sizeof`/`alignof`（用同一
探针或 `cmp_disasm`）；`kBackendStateBytes` 的最终取值。

## 深度审查补充（第二轮，2026-10-03）

1. **工作树不是 `acc5e7b`（最高优先，推翻本计划的基线前提）**。`git describe --dirty` = `acc5e7b-dirty`；
   24 个已跟踪文件有未提交改动 + 若干未跟踪新文件，分三组在飞工作：
   - ancillary vr-task-tag 迁移：新增 `session/ancillary/vr_task_tag.{hpp,cpp}`、改
     `ancillary_controller.hpp`/`ancillary_policy.hpp`/`cve_2026_43499_backend.cpp`（vr 写移入行为）、
     `tests/ancillary_test.cpp`、新增 `tests/host/ancillary_stub.cpp`、`src/Makefile`；
   - 占位 backend：新增 `session/backend/cve_2026_{31431,43503,23274}_backend.hpp`、改
     `route/component_catalog.hpp`/`backend_contract.hpp`/`backend_policy.hpp` 与测试；
   - TCP 5.x geometry：`profile/model.h`、`profile/binary.{h,cpp}` 新增 `tcp_*` 字段。
   这些**正好落在 Phase 0 要改的文件上**。**用户确认：在飞改动保留，不归位、不回退**；因此 Phase 0 的
   起点状态 = `acc5e7b` + 当前在飞改动，重写在其上叠加。
   - **基线落法（已定 a：提交 WIP/前置提交）**：把在飞改动提交到本分支（内容不变；按功能可拆 2–3 个
     提交，或一个 checkpoint 提交），该提交记为 **`B0`**。用 `git worktree add <tmp> B0` 在干净检出上
     `make -C src ghostlock` 构建**基线二进制**，记录 `git rev-parse B0` + 二进制 sha256。现有
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
3. **`src/CMakeLists.txt` 仅用于 CLion 自动补全（用户确认），不是构建面**。权威构建面是 `src/Makefile`
   （Gradle native 任务 `commandLine("make", "ghostlock")`）。它以 `SOURCES` 给 IDE 建索引，目前缺
   `route_middleware.cpp`/`ancillary/*`/`profile/binary.cpp`/`root_child_frontend.cpp`/`backend/*`/
   `run_state.cpp`。处理：**保留，并随 Phase 0 新增/改名文件同步 `SOURCES`**（加
   `core/session/core_session.cpp`、`core/session/backend/cve_2026_43499_state.cpp`），只为 IDE 索引准确；
   **不进门禁、不作为“构建成功”的证据**。
4. **并行计划重叠需排序**：`docs/analysis/flexible-kernel-rw-primitive-plan.md` 是 Phase C（`KernelMemory`）
   的已细化版本，且明确“值走 session、不给 `attack_write` 加参数”以保 disasm；`5x-tcp-geometry-plan.md`
   改 profile 字段与 TCP 路由；`placeholder-backend-survey.md` 对应占位 backend。顶层计划未给排序，
   存在同一改动两处设计与先后冲突风险。
5. **`session_layout_test` 细节**：`offsetof` 会触发 `-Winvalid-offsetof`（本项目要求零告警），需在测试内
   局部 `#pragma clang diagnostic ignored`；测试可 header-only（只 `sizeof`/`offsetof`，不构造、不链接），
   若构造则需链 `heap_context`/`victim_context`/`native_resource`/`run_state` 等。
6. **构建面清单补齐**：`HOST_ATTACK_DATAFLOW_SOURCES` 必须新增
   `core/session/backend/cve_2026_43499_state.cpp`；`NATIVE_HOST_TESTS` 注册 `session_layout_test`；
   `HDRS` 加两个新头；`CXX_SRCS` 加两个新 `.cpp`（`core_session.cpp`、`cve_2026_43499_state.cpp`）。
7. **文档已在 dirty 集合中被改**：`AGENTS.md`、`src/core/README.md`、`docs/development/adding-a-component.md`
   等已改动；Phase 0 的文档同步应在归位后的新基线上做，避免与在飞改动冲突。
8. **上位计划 Phase 0 描述与 ADR-0002 矛盾**（原写 CoreSession 含 `profile`/`addresses`），已改。
