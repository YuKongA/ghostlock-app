# ADR-0001：顶级命名空间评估与漏洞原语模块化

- 状态：Proposed（待维护者确认）；架构审查（第三轮）发现 A–I，A 的 profile 归属方向已定（本文件
  §10/§12：中性 document + backend/platform 注册 schema），B–I 采用前需修订，见
  `docs/analysis/top-level-architecture-rewrite-plan.md` 的“架构审查”。
- **ADR-0004 修订本文件**：§1 的“四条正交轴”表述（R2）、§7 的 `handoff`（R3，顶级轴改 `terminal`）、
  §14 的 `ancillary`（R4，**保持顶级中性机制**，行为归 `platform::vivo`）、顶级 `kernel` 并入 `memory`（R6）、
  §21 的门禁（R7）。
- 本 ADR 只记**决策**；F/O/C/E/A/CF/T 等的闭包状态见 `architecture-findings-register.md`，审查原文见
  `architecture-review-log.md`。
- 日期：2026-10-03
- 基线：`acc5e7b`（`vr-ko-bypass-dev`）
- 相关：`docs/analysis/top-level-architecture-rewrite-plan.md`、
  `docs/analysis/flexible-kernel-rw-primitive-plan.md`、`docs/development/design-philosophy.md` §2/§3/§5

## 背景（Context）

对 `src/core` 命名空间级结构做了一次清点（见重写计划的现状树），结论：

- 顶级划分主要按**执行角色/状态归属**切，而非按 Parnas 的“最可能变化的决策”切（design-philosophy §2）。
- `session` 承担 5 类职责（状态、后端过程、前端 handoff、阶段机、victim/ancillary），是最重的 god
  namespace。
- **最可能变化的漏洞原语没有模块**：`PiRace`(race) + `WriteRequest/PayloadWriteLayout/HeapContext`
  (memory) + `prepare_skb_payload`(support) + route 类(route) + 调用点(session::backend)。
  这正是 `KernelMemory` 无处安放的根因。
- `memory` 混合三轴（地址解析 / spray 页状态 / 写请求编码）；`route` 把组合层
  （catalog/pipeline/orchestrator/identity）与 middleware 执行混在一起。
- 组件身份与执行策略跨命名空间（`route::backend::Cve2026_43499` vs
  `session::backend::Cve2026_43499Policy`），组件不局部。
- `kernel` 常量与 `profile` 覆盖是**有意**的双层，但 `kernel_offsets`（原始）与类型化 view 并存，
  新增字段有漂移风险（§3 SSOT）。

硬约束（会限制任何整理）：PI 窗口零间接调用；`cmp_disasm` 的 8 个攻击函数要求字节稳定或已复核差异；
`ExploitSession` 字段布局固定（攻击函数栈偏移）；仅允许 `g_exploit_session` 一个可变全局。

## 决策（Decision）

维护者确认“完整重构可接受”，故**采用 Option C：一次设计的受控完整重写**（design-philosophy §5：
允许受控重写，但必须是**有立项**的工程——完整目标设计、单分支、明确的验证与归档）。

判据（关键，兼容性驱动）：
- **中性可复用**：中性件必须能被一个**原语完全不同**的 backend 复用；内嵌 43499 机制/目标的
  不算中性（证据：`docs/analysis/placeholder-backend-survey.md`）。
- **按功能切轴**：按“最可能一起变化的功能”聚合，而非按执行角色/文件。

1. **顶级目标（ADR-0004 定版）**：`contract`（真通用值/接口）/ `memory`（内核地址词汇 + 地址数学，
   原 `kernel` 并入，R6）/ `session`（通用状态，ADR-0002）/ `pipeline`（组合：`Pipeline<Backend, Terminal>` ·
   catalog · orchestrator，R12）/ `backend`（私有：`backend_profile` · primitive · route · victim · service ·
   steps · `backend_terminal` 选择 · 可选发现 `kernelsnitch`/`perf_find_task`）/ `platform`（横切对策：
   `abi` 内核 ABI 偏移 + 设备 phys · `runtime` 设备探测 · `vivo` 厂商 vr）/ `ancillary`（中性阶段钩子机制，
   R4）/ `terminal`（跨 backend 终止接管策略：root_child/umh_forward/file_write/panic，R3）/ `profile`
   （GLK1 容器 framing + 中性 Document，ADR-0003）/ `support`。
   `contract` 另含**可选能力接口** `KernelMemory` 与 `AddressDiscovery`（实现可随 backend，复用后再共享）。
   **结构表述（A1/R2）**：**2 装配轴** `backend`×`terminal`；**backend 内** `route`（含 primitive/victim，按 profile 选）；
   **2 横切** `platform`（profile 门控）· `capabilities`（可选能力）。不再用“四条正交轴”。
2. **`middleware → route`，并随 backend**：术语统一 `MiddlewareKind→RouteKind`、`MiddlewarePolicy`
   并入 route 契约、`middleware_available→route_available`、`ComponentSelection.middleware→.route`；
   **顶级 `route` 取消**。
3. **漏洞机制/route/victim 全部 backend 私有**：`PiRace`/payload/`WriteRequest`/`HeapContext`/
   `RouteController`/`RouteStatus`/三种 route/victim 协议/slide/spray 都是 43499 专用；
   64560/31431/43503/23274 **不复用**（survey）。
   **例外（归 platform，跨 backend 强制需要的设备/厂商/内核能力）**：内核 ABI 偏移
   （`task`/`cred`/`seccomp`/`struct page`/`init_cred`/`selinux`）、设备 phys 默认、vivo vr.ko。
   **可选发现（`kernelsnitch`/`perf_find_task`）不强制共享**：作 `contract::AddressDiscovery` 的实现，
   默认随使用的 backend；有第二个 backend 复用再提升为共享发现库（避免过早抽象）。
4. **`memory` 只留真中性地址数学**（`SocFamily`/`phys_load`/`phys_offset`/`data_alias`/`canon_addr`/
   `is_direct_ptr`/`in_direct_map` → `memory::AddressSpace`）；符号别名（`init_cred_image` 等）、
   spray/heap 归 backend 私有。
5. **`contract` 收缩为真通用**：`RunResult`/`StageResult`/`RunCode` + **可选能力的公共父接口**
   `CapabilityKind`/`Capability`（concept）/`Capabilities`（句柄集合，`CoreSession` 的字段），
   以及具体能力 `KernelMemoryOps`/`AddressDiscoveryOps`。`RouteStatus`/`RouteRunResult`（route 私有）、
   `VictimChain`（victim 私有）**移出** → backend 私有。
   组合根：`CoreSession` 持 `Capabilities`，establish 后只读；组件取窄视图（backend `CoreSession&`、
   ancillary `Capabilities&`）；无需到处传 context。
6. **`KernelMemory` 是可选能力接口**（`available()` 门控），实现在 backend；无 R/W 的 backend
   （如 23274）不提供，ancillary 自动惰性。
7. **`Pipeline<Backend, Terminal>`**：取消独立 route 轴；route 由 backend 内部按 profile 选（含
   fallback）；`handoff`（原 frontend/root_child）是 backend 可选的**终止动作**（43503 写文件、
   23274 panic 不经过 root child）。**ADR-0004 R3/R12**：顶级轴为 `terminal/`（root_child/umh_forward/
   file_write/panic），`handoff` 降为 `terminal::root_child` 的内部机制；pipeline 编排
   `Pipeline<Backend, Terminal>`（先 `B::run(session, RootedChild&)`，再 `T::run(session, RootedChild&)`），
   catalog 按 `(backend, terminal)` 组合枚举。
8. **`runtime → pipeline`（组装与分派）**：只留组装（Pipeline/RunResult · catalog · orchestrator ·
   identities）。（“组合根”一词专指 `CoreSession`，见 §5；pipeline 负责编译期装配与运行期分派。）
9. **拆分 `attack::*`（已定）**：按功能拆散——`in_direct_map`→`memory`；`timer_*`→`support`；
   `perf_find_task`→backend（`contract::AddressDiscovery` 的可选实现）；环境探测（`check_selinux_off`/
   `enforce_readable`/`process_has_seccomp`）、`apply_iomem_cache`（设备 direct-map）→`platform`
   （设备运行时）；
   `slab_drain`→backend；`install_profile`/`resolve_profile_addresses`→bootstrap；`write_root_script`
   →`handoff`。
10. **offset SSOT（P6）纳入本次（已定，按注册模型重述）**：唯一权威是**中性 document**（GLK1 v2 的
    `sections[name→fields[name→u64]]` + presence）。每个字段由**恰一个 owner schema** 声明（backend 或
    platform），typed view/访问器是从 document 的纯派生；不存在第二处字段映射。`kernel_offsets` 不再是
    共享权威结构，而是 43499 后端注册出的 view。约束 `sizeof()` 与 disasm 不变量。注册模型、字段划分
    与三端一致性见 **ADR-0003**（`docs/analysis/adr/0003-profile-schema-registration.md`）。
11. **`ExploitSession` 通用化（ADR-0002，本次同步做，已定）**：否则机制无法下放 backend
    （会 `session→backend`）。设计见 `docs/analysis/adr/0002-exploit-session-generalization.md`。
    `CoreSession` 框架字段**不含 `profile`**（见 12）。
12. **`profile` 分层（已定，注册模型）**：`binary_profile`（GLK1 容器：magic/version/组件 id/sections）中性，
    且**必须能表达任意 route/path 的任意参数**——因此 profile 包含具体后端参数是设计要求，不是缺陷；
    做法是容器只存中性 document（`sections[name→fields[name→u64]]` + presence），**各 owner 注册自己的
    字段 schema/typed view** 并对同一 document 派生：backend 注册漏洞专属的 route/victim/payload/execution/
    target 字段（`TargetProfile`/`kernel_offsets` 是其 view，不再是共享权威）；`platform` 注册跨 backend 的
    设备/内核字段（vr 字段 `vr_guard`/`vr_sys_exit_tp`/`vr_tracepoint_funcs`、内核 ABI 偏移 `task`/`cred`/
    `seccomp`/`struct page`/`init_cred`/`selinux`、设备 phys 默认）。**每个字段恰有一个 owner**（backend 用
    `platform` 的 view 时依赖 platform，不重复声明）。`RuntimeConfig` 泛化（不再 `apply_profile(TargetProfile)`）。
    Schema/Document/注册机制/字段表/未知字段策略见 **ADR-0003**。
13. **`ResolvedAddresses` 拆分（已定）**：通用地址数学 → `memory::AddressSpace`；符号别名
    （`init_cred_image` 等）随 backend。
14. **ancillary 分层（已定，修正）**：框架（`AncillaryController`/`AncillaryStage`）中性；
    **`AncillaryContext` 退役**，行为改取 `contract::Capabilities&`（窄视图，组合根提供）；
    **vr 行为（`AncillaryKind`/`VrGuardPolicy`/`VrTaskTagPolicy`）是跨 backend 的平台能力**
    （vivo vr.ko 对策），归 `platform`（或作为跨 backend 的 ancillary 扩展），**不属任何单个 backend**；
    其配置随平台/profile 作用域，仅依赖 `KernelMemory` 能力。**ADR-0004 R4**：`ancillary` 保持顶级
    （中性机制），行为 → `platform::vivo`，注册表由组合点注入。
15. **基础设施去 43499 夹带（已定）**：
    - `kernel/target.h` 的 43499 默认符号偏移（`INIT_TASK_OFF`/`INIT_CRED_OFF`/`ROOT_TASK_GROUP_OFF`/
      `SELINUX_*`/`FOPS_OFF`/`TASK_*`/`SKB_*`/`FAKE_TASK_*`）与 `target_constants.hpp` 的
      `payload::*`（伪造 waiter/fops 布局）→ backend；`kernel` 只留通用地址布局
      （`DIRECT_MAP_*`/`P0_*`/`KernelAddress` 域/`struct page`），且按 **ADR-0004 R6** 并入 `memory`。
    - `support/util.cpp` 与 `support/decls.hpp` 的 spray/页/PI punch（`prepare_skb_payload`/
      `prepare_good_kernel_page`/`prepare_kernel_page`/`stash|activate|discard_prebuilt_page`/
      `quarantine|release_quarantined_reclaim_sockets`/`close_reclaim_sockets`/`clone_child`/
      `clone_leak_child`/`open_memfd`/`kill_child`/`clone_memfd`/`prepare_ctxs`/`sched_setattr_tid`/
      `init_p0_profile`）→ backend primitive；`support` 只留 RAII/Result/time/number_parse 与
      通用声明（`log_*`/`read_first_line`/`fail_stop_dirty_race`/`disable_rseq_for_thread`/
      `put32`/`put64`/`futex_op` 薄封装）。
    - `profile/binary.cpp` 的 43499 字段表 → backend profile；容器 framing（magic/version/sections）
      留 `profile`。
    - **聚合头去耦（F10）**：`common.h`（万能 include，拉入 payload_builder/heap_context/pi_race/
      exploit_session/kernelsnitch/profile accessors 等）与 `kernel/offset.h`/
      `kernel/runtime_struct_offsets.h`/`profile/accessors.hpp` 是 **43499 偏移访问面** →
      归 backend；中性文件不得 include backend 私有头；`common.h` 拆为通用/backend 两部分。
16. **组合层接口形状（F1–F5，已定）**：
    - `Pipeline<Backend, Terminal>`：`run(session::CoreSession&)`；**接口不得出现 `kernel_offsets`/
      `VictimChain`/route 类型**（均为 backend 私有）。
    - `BackendExecution<B>`：`run(CoreSession&, terminal::RootedChild&) -> StageResult`（去掉 middleware 模板；
      终端输入经中性 `RootedChild` 移交，R10/R12）。
    - backend `run` 内部：从状态取 `TargetProfile` → 按 profile 选 route（`switch` 到
      `run_steps<Route>` 的编译期实例，含 fallback）→ victim → 调用选定 terminal。
    - `ComponentSelection` 收为 `{BackendKind, TerminalKind}`（terminal 是显式维度，R12）；route 维度移入
      backend profile；`DispatchTarget` 按 `(backend, terminal)` 组合。
    - `RunStage::{None,Backend,Terminal}`（原 `Frontend` 改名）。
17. **传输/入口去 43499 绑定（F6/F7）与 kernelsnitch 归属（已定）**：
    - `binary_profile` 只做 framing（magic/version/组件 id/sections）→ **通用 document**；
      sections → `TargetProfile` 的绑定归 `backend::cve_2026_43499::backend_profile`（R11）；`profile_entry::read_glk1_*`
      同样不再产出 `kernel_offsets`。
    - `profile/entry.cpp` 的 “v3” 注释修正为 v2（以 `binary.h` 为准）。
    - `kernelsnitch/` 上游已冻结、不再更新 → **可改写**；是 `contract::AddressDiscovery` 的**可选
      实现**，默认随使用的 backend（与 `perf_find_task` 同类），**有第二个 backend 复用再提升共享**；
      且**必须在顶级重构拆分之后**进行，并保证测试（保留/扩展 `kernelsnitch_scan_bounds_test`，
      补拆分子件与发现契约的 host 测试）；改写不得改变泄漏结果（真机/行为门禁）。
    - **Kotlin/wire 同步（F11）**：`app/.../data/route/*`（route 注册/`fromWire`）与
      `Profile.kt`/profile document model 是 43499 route/字段 schema → 随 backend 分层；`profile-core`
      的 wire framing 通用。双端一致性测试（`route_catalog_test`/`RouteCatalogAgreementTest`、
      `ProfileRoundTripTest`）同步。
    - **提取器分层（F12）**：`tools/extract_rs` 的符号/结构表（`symbols.rs` 的 `SYMBOLS`/
      `STRUCT_FIELDS`）是 43499 专用 → 归 backend 的提取规则；`boot.rs`/`btf.rs`/`kallsyms.rs`/
      `--format` 框架通用。
18. **剩余 43499 夹带（F13–F18，穷举文件级审计，已定）**：
    - F13 `support::run_state` 的 **step 词表**（`w1a`/`w2b`/…，↔ Kotlin `RunStateCodec`）是 backend
      专属 → 词表归 backend；`run_state` 机制留 `support`。
    - F14 `profile/macros.h`（`VR_TAG_B_OFF`）→ `platform`（vivo vr；跨 backend）。
    - F15 `kernel/constants.hpp` 的 `SKB_*`/`MM_*`/`ORDER3`/`FOPS_TABLE_OFF`/`FAKE_TASK_*`/
      `FAKE_WAITER_*`/`PSELECT_*` → backend primitive/route；只留 `PAGE_*`/`g_direct_map_end`
      （与通用 `local_sched_attr`/`TASK_COMM_LEN`）。
    - F16 `profile/accessors.hpp`（`slide_*`/`kernelsnitch_collisions`）→ backend。
    - F17 `handoff::RootChildPolicy::run(session, const VictimChain&)` 泄漏 backend `VictimChain`
      → handoff 接口泛化（或 terminal 归 backend）。
    - F18 `route::reserve_standard_io`（通用）→ `support`。
19. **编译/测试/构建接线（C1–C3，每阶段强制）**：
    - **C1 编译边界**：搬迁必须重画 `#if defined(__ANDROID__)`（现 9 处）与“header host-safe / body
      Android-only”划分，保证 `native-host-tests` 仍可链接。
    - **C2 host 数据流 harness**：`HOST_ATTACK_DATAFLOW_SOURCES` 与影子头
      `tests/host/include/attack/ops.hpp` 随 `attack/ops` 拆分及 backend/route/pipeline 签名变化
      （F1–F5）重接，避免静默断链（ancillary 已踩过）。
    - **C3 构建面同批**：`src/Makefile`（`CXX_SRCS`+各 host 测试依赖）、`src/CMakeLists.txt`（`SOURCES`）、
      `src/.clang-tidy`、`app/build.gradle.kts`（`routeFieldPaths`）、`tools/extract_rs/src/{lib,main}.rs`
      与 crate 测试。
20. **错误分层与 fail-closed（E1–E3，已定）**：
    - E1 初始化/构造失败（profile 解码、backend 状态构造）→ `FatalError`/`Rejected`，在 PI 窗口外。
    - E2 运行时能力失败（`KernelMemory::available()==false`、通道建立失败）→ fail-closed、非致命。
    - E3 攻击步骤失败 → `StageResult`/`RunResult`（带 stage）；**异常与间接分派绝不进 PI 窗口**。
    另：L 日志文本/顺序 + `run_state` 阶段标记顺序保持；D 结构图/README/AGENTS 单一权威同步；
    M 搬迁按依赖排序（Phase 0→A→A2→A3→B）。**控制流时序与失败传播见 ADR-0004 R13–R17**。
21. **门禁按批推进（ADR-0004 R7）**：Phase A 机械搬迁分小批，每批 host 全绿 + NDK 零告警；**Phase A2
    （语义收敛）独立完整门禁**：host + NDK 零告警 + `cmp_disasm`（更新 `TARGETS` 与基线、逐条复核）+
    真机（Xperia 5.15；vivo 6.1 协调后）；A3 独立（带测试）；B host + Kotlin 对拍；C 另 PR。
    以“上一批验证通过再进下一批”为准，不再“以最终门禁为准”。
22. **明确保留**：PI 窗口规则、四段生命周期、契约接口语义、GLK1 v2、W1/W2/W3 行为与顺序。
    （`kernelsnitch/` 见 §17：上游冻结，可在重构后改写，须保测试。）

## 备选方案（Considered Options）

- **A：现状不动，仅新增 `KernelMemory`。** 塞进 god namespace，加剧变化轴错位。**拒绝**。
- **B：原语模块 + 分阶段小步收拢。** 风险最低，但长期残留 P1/P4/P5。**让位于 C**。
- **C（采用）**：一次设计的完整重排，**并纳入 P6 offset SSOT 与 `attack` 拆分**（两者都是结构/语义
  收敛、不改行为）。代价：失去 bisect 粒度、门禁一次性变重。
- **C-（部分）**：把 `KernelMemory` **功能**也并进同一次重写——**不采用**：功能与结构混在一次门禁
  失败难归因；`KernelMemory` 延后到结构稳定后单独 PR（已定）。

## 后果（Consequences）

正面：
- 顶级职责沿功能/变化轴切开；原语、组合、状态、扩展框架各有其位；新增组件触点收敛。
- `KernelMemory` 的 B/C/回退替换只动 `contract`（接口）+ `backend`（实现与机制），不波及外围。

负面/风险：
- **不可 bisect**：结构+符号改名一次性落地，若门禁失败要整批定位；缓解：机械搬迁与功能分离提交、
  保留完整 diff 与证据链。
- **符号名全变**：`cmp_disasm` 的 `TARGETS` 与新基线需同步；期望“同形（指令形状一致）+ 已复核
  的地址/符号差异”，而非 strict IDENTICAL。
- LTO 重排可能移动代码/数据地址，差异需逐条折算复核。

**已知遗留（本次不改，记录）**
- **多 backend 前置 = ADR-0002**：`route/pipeline.hpp:76` 的 `Pipeline::run(ExploitSession&)`
  硬编码单一 session 类型，而 `ExploitSession` 内嵌 43499 专属状态（`PiRace`/`HeapContext`/
  `VictimContext`）。要让机制/route/victim 真正下放 backend，**必须先**把 `ExploitSession` 通用化
  （模板化或通用槽）——这是本重写的**前置**，另立 ADR-0002 并与之同步。
- **catalog↔backend 身份环**：`pipeline` 的 catalog 枚举 backend 身份，backend 又 include catalog
  做 static_assert → 编译期环。本次保留；缓解方向是“身份声明在 component、注册表在 pipeline”，
  避免 back-include。

后续动作：
- 先按“架构审查（第三轮，原文见 `architecture-review-log.md`）”A–I 修订本 ADR（尤其 A `platform`/backend profile 归属、B `memory→profile`
  解耦、C ancillary 注册、D handoff/terminal 边界、G route 私有化与 wire 全局 id）。
- 依重写计划执行：单一重写分支 → 机械结构重排 → 契约/文档同步 → 功能（`KernelMemory`）→ 一次完整门禁。
