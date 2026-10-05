# A2-4 / A2-5 逐文件实施计划（platform/ancillary 分层 · profile 分层 · 传输/入口去绑定 · common.h 去耦）

> 状态：草案，待维护者认可后实施（L 级改动，见 docs/development/engineering-standards.md §1.2）。
> 分支：vr-ko-bypass-dev。任务范围：S1 框架收敛的 A2-4 与 A2-5。
> 权威上游：docs/analysis/top-level-architecture-rewrite-plan.md（迁移映射/Phase A2 分批/T1–T4）、
> docs/analysis/branch-plan.md（顺序/基线）、docs/analysis/adr/0001–0004（R1 依赖边、R4/R5/R8/R11/R14）。
> 本文只给设计，不改任何源码；所有改动以批次独立门禁推进。
>
> 术语：R1 = ADR-0004 允许依赖边；R4 = ancillary 中性机制、行为归 platform::vivo；
> R5 = platform 内部分层；R11 = backend 同名子模块加 backend_ 前缀；R14 = 编译期
> AncillaryPolicyList、无运行期注册表。未验证 表示本计划未能从现有代码/文档确认，需实施时核实。

## 0. 结论摘要

- A2-4 拆 6 批：中性 ancillary 机制、厂商行为下放 platform::vivo、platform::abi owner
  schema/view、memory/target.h 常量面拆分（可选）、profile 类型物理迁移（受 memory→profile 阻塞，
  建议延后）、memory::AddressSpace 解耦 TargetProfile（前置）。
- A2-5 拆 5 批：entry 产出中性 profile::Document、pipeline/backend 接口去 kernel_offsets、
  GLKv3 解码去 backend/pipeline、common.h 逐文件去耦、include-firewall + fake backend。
- 最大风险：把 profile::kernel_offsets/TargetProfile 物理搬去 backend 会撞上
  memory::AddressSpace 对 profile::TargetProfile 的依赖（memory/address_space.h:4），
  而 memory→backend 被 R1 禁止；仓促做会改布局、连带 8 个攻击函数、session_layout_test 与
  address_space_test。因此本计划把「函数/模块归位」与「类型物理迁移」分开：归位不搬类型，
  类型迁移单列并置后。
- 建议第一批 = A2-4-1（中性 ancillary 机制）：blast radius 小、不碰 wire/布局、不动攻击函数，
  只改 src/core/ancillary/* 与 steps.cpp 调用点；host/NDK/lint/cmp 可判定，是 A2-4-2 的前置。

---

## 1. 现状盘点（文件级）

### 1.1 platform

| 文件 | 现状 | 判定 |
|---|---|---|
| src/core/platform/runtime.hpp / .cpp | 设备运行时探测：apply_iomem_cache、check_selinux_off、enforce_readable、process_has_seccomp。namespace ghostlock::platform::runtime。已是 A2-1/A2-2b 落点。 | 留（命名空间已对；文件可留平铺，无需子目录） |
| src/core/platform/device_facts.hpp / .cpp | 43284 endgame 的设备/固件事实探测（B5-7）。namespace ghostlock::platform。 | 留（不属于 A2-4；可后续归 platform::runtime，本计划不动） |
| src/core/platform/abi* | 不存在。内核 ABI 偏移与设备 phys 目前散在 profile/model.h（TaskStructOffsets/CredTemplate/KernelOffsets/KernelMisc.phys）、memory/target.h 与 profile/binary.cpp 字段表。 | 新增 |
| src/core/platform/vivo* | 不存在。厂商对策目前在 ancillary/vr_guard.*、ancillary/vr_task_tag.*、profile/macros.h、profile/model.h（vr_guard/vr_tracepoint_funcs/vr_sys_exit_tp）。 | 新增 |

### 1.2 ancillary

| 文件 | 现状 | 判定 |
|---|---|---|
| src/core/ancillary/ancillary_policy.hpp | 混合中性机制与厂商词汇：AncillaryStage（中性）、AncillaryContext（R4 要求退役）、AncillaryZeroFn（中性写原语）、AncillaryKind{VrGuard,VrTaskTag}（厂商）、AncillaryPolicyFor（概念）。 | 改：中性部分留 ancillary_policy.hpp；AncillaryKind 迁 platform::vivo；AncillaryContext 退役/改名 |
| src/core/ancillary/ancillary_controller.hpp | 硬编码 AncillaryPolicyList = tuple<VrGuardPolicy,VrTaskTagPolicy>；include backend/cve_2026_43499_state.hpp；apply() 读 cve43499_state(session).profile 作 gate。违反 R1（ancillary 只允许 contract,memory,support）。 | 改：去 backend 依赖；registry/gate 由调用方注入；文件改名 ancillary/controller.hpp（可留名） |
| src/core/ancillary/ancillary_controller.cpp | 仅 include 头以强制 Android/clang-tidy 编译（无函数体）。 | 改（include 路径）或 删（若新机制 header-only 已被 backend TU 覆盖） |
| src/core/ancillary/vr_guard.hpp / .cpp | VrGuardPolicy + plan_vr_guard（PreSpawn 清 __tracepoint_sys_exit.funcs）。厂商对策。 | 迁 → platform::vivo |
| src/core/ancillary/vr_task_tag.hpp / .cpp | VrTaskTagPolicy + plan_vr_task_tag（PostSpawn 清 per-task tag A/B）。厂商对策。 | 迁 → platform::vivo |

### 1.3 profile

| 文件 | 现状 | 判定 |
|---|---|---|
| src/core/profile/document.hpp | 中性 framing：Value/Entry/Section/Document，不命名字段、不解释 route。 | 留 |
| src/core/profile/schema.hpp | 中性 owner 机制：FieldSpec/SchemaDefinition/bind<Schema>/DecodeMode。 | 留 + 扩展（增 bind_all<SchemaList>，见 §3.3） |
| src/core/profile/glkv3.hpp / .cpp | MPack 容器 codec（decode/encode、canonical、fail-closed）。中性。 | 留 |
| src/core/profile/glkv3_parse.hpp / .cpp | v3→v2 桥：include backend/cve_2026_43499/glkv3_schema.hpp、backend/cve_2026_43284/glkv3_schema.hpp、pipeline/component_catalog.hpp。profile→backend/pipeline，违反 R1；token↔id 映射（terminal_from_token/backend_from_token）也在容器层。 | 改/迁绑定：容器只做 framing→Document；组合 schema 与 token 映射上移（A2-5-3） |
| src/core/profile/binary.h / .cpp | v2 容器 framing + bind_document（把 sections 绑定到 kernel_offsets）+ v2 writer 字段表（backend 字段）。include backend/.../schema.hpp，profile→backend。 | 改：framing 留 profile；bind_document 与字段表迁 backend::cve_2026_43499::backend_profile（A2-5-2） |
| src/core/profile/model.h | execution_settings、RouteKind/kRouteCatalog、ProfileMeta、TaskStructOffsets、CredTemplate、KernelOffsets、KernelMisc、RouteGeometry、kernel_offsets、TargetProfile、typed layouts、VrGuardLayout。物理冻结（sizeof(kernel_offsets)==520、sizeof(TargetProfile)==784）。 | 目标迁 → backend/cve_2026_43499/backend_profile/model.hpp；本计划建议延后（见风险 R-1） |
| src/core/profile/entry.h / .cpp | 传输入口：stdin/帧/文件 → read_glk1_* → 直接产出 profile::kernel_offsets。 | 改：产出中性 profile::Document（A2-5-1） |
| src/core/profile/accessors.hpp | slide_* 访问器，读 cve43499_state(...).addresses、依赖 runtime_struct_offsets.h。43499 专属。 | 迁 → backend（backend_profile 或 bootstrap） |
| src/core/profile/runtime_struct_offsets.h | init_task()/task_cred_off()/fake_task_*() 等，读 cve43499_state(...).profile。平台 ABI 偏移 + 43499 访问。 | 迁/拆 → platform::abi（ABI 偏移）+ backend（slide/fake 布局）；见 A2-4-3 |
| src/core/profile/macros.h | VR_TAG_B_OFF（构建期可覆写的 vivo 常量，F14）。 | 迁 → platform/vivo/macros.h |
| src/core/profile/binary.h 中的组件 id 常量 | kTerminalRootChild/kBackendCve2026*/kStepSet*，与 pipeline::BackendKind/TerminalKind/StepSetKind 重复。 | 未收敛：身份词汇应归 contract（ADR-0004 S2），本计划不处理，标注 未验证 |

### 1.4 common.h（聚合头）

src/core/common.h 54 行，拉入：memory/offset.h（target config）、profile/runtime_struct_offsets.h、
memory/address_space.h、memory/payload_builder.h、session/runtime_config.h、support/time.h、
memory/heap_context.h、race/pi_race.h、session/core_session.hpp、memory/constants.hpp、
kernelsnitch/utils.h（pr_* 宏）、profile/accessors.hpp、support/decls.hpp、support/timing.hpp、
backend/cve_2026_43499/route/route_api.hpp，以及一批系统头。

18 个 include 者（grep '#include "common.h"' src/core）：

| 文件 | 备注 |
|---|---|
| src/core/main.cpp | 需要 framing/entry/CLI 之外的少量系统头 |
| src/core/session/runtime_config.cpp | 环境/字符串 |
| src/core/race/threads.cpp、race/threads.hpp | PI 线程、session 状态 |
| src/core/ancillary/vr_guard.cpp、ancillary/vr_task_tag.cpp | 迁 platform/vivo 后需重列 |
| src/core/support/util.cpp | spray/页/PI punch 大量 cve43499_state |
| src/core/backend/cve_2026_43499_backend.cpp | setup/route 分派 |
| src/core/backend/cve_2026_43499/steps.cpp | W1/W2/W3、ancillary 调用 |
| src/core/backend/victim/victim_process.hpp / .cpp | 子进程协议 |
| src/core/backend/cve_2026_43499/primitives.cpp、bootstrap.cpp | 写原语/装配 |
| src/core/terminal/root_script.cpp、terminal/root_child.cpp | handoff |
| src/core/backend/cve_2026_43499/route/tcp_zerocopy_route.cpp、select_stack_route.cpp、multicast_waiter_route.cpp | route 实现 |

common.h 同时把 backend 私有访问面（profile/accessors.hpp、runtime_struct_offsets.h、
route_api.hpp、cve_2026_43499_state）拉进中性文件，属 ADR-0001 §15/F10 的聚合头去耦。

### 1.5 留/迁/改/删总表（含旧→新）

见 §5 的逐文件表，这里只给方向：新增 platform/abi、platform/vivo；迁 vendor 三件套与
slide/macros；改 ancillary 机制、entry、binary/glkv3 绑定、pipeline/backend 签名；删 common.h
（以及可能的 ancillary_controller.cpp）。

---

## 2. 目标布局

### 2.1 目录与 namespace

~~~text
src/core/
├── contract/                     ghostlock::contract       （留；+ 可选 AncillaryWriteOps）
├── memory/                       ghostlock::memory         （留；AddressSpace 需解耦 TargetProfile）
├── session/                      ghostlock::session        （留）
├── pipeline/                     ghostlock::pipeline       （留；接口去 kernel_offsets）
├── platform/
│   ├── abi.hpp                   ghostlock::platform::abi   （新增：Schema + View）
│   ├── runtime.hpp/.cpp          ghostlock::platform::runtime（留）
│   ├── device_facts.hpp/.cpp     ghostlock::platform        （留）
│   └── vivo/
│       ├── schema.hpp            ghostlock::platform::vivo  （新增：Schema + View）
│       ├── vr_guard.hpp/.cpp     ghostlock::platform::vivo  （迁）
│       ├── vr_task_tag.hpp/.cpp  ghostlock::platform::vivo  （迁）
│       ├── macros.h              ghostlock::platform::vivo  （迁，宏保留全局名 VR_TAG_B_OFF）
│       └── registry.hpp          ghostlock::platform::vivo  （新增：VivoAncillaryPolicies）
├── ancillary/
│   ├── ancillary_policy.hpp      ghostlock::ancillary      （改：中性）
│   └── controller.hpp/.cpp       ghostlock::ancillary      （改：无 backend 依赖）
├── backend/cve_2026_43499/
│   ├── backend_profile/          ghostlock::backend::cve_2026_43499::backend_profile
│   │   ├── model.hpp             （迁 profile/model.h，延后）
│   │   ├── bind.cpp              （迁 binary.cpp::bind_document，A2-5-2）
│   │   ├── schema.hpp            （现 backend/cve_2026_43499/schema.hpp → backend_profile/）
│   │   ├── glkv3_schema.hpp      （同上）
│   │   └── accessors.hpp         （迁 profile/accessors.hpp）
│   └── …（route/victim/steps/bootstrap 留）
├── profile/                      ghostlock::profile        （仅 framing + Document + schema 机制）
│   ├── document.hpp / schema.hpp / glkv3.* / binary.*（framing 部分）
│   └── entry.*（改：产 Document）
└── support/                      ghostlock::support
~~~

命名要点：
- namespace 已对的（platform::runtime）不搬文件；platform::abi/vivo 是命名空间要求，
  不必强求子目录（platform/vivo/ 用子目录是因为含多文件）。
- R11：backend 内与顶级同名者加 backend_ 前缀，因此 backend::cve_2026_43499::backend_profile
  （不是 profile）。本计划把 schema.hpp/glkv3_schema.hpp 也放进 backend_profile/，
  命名空间仍可用 ghostlock::backend（现状）或收敛到 ...::backend_profile；未验证：是否要求
  schema 类型名也改，若只搬路径/命名空间不影响 wire。

### 2.2 目标依赖图（R1）

~~~mermaid
flowchart TB
  support --> memory
  memory --> contract
  contract --> session
  contract --> ancillary
  support --> platform
  memory --> platform
  profile["profile framing + Document"] --> platform
  ancillary --> platform
  contract --> platform
  platform --> backend
  ancillary --> backend
  session --> backend
  terminal --> backend
  profile --> backend
  backend --> pipeline
  terminal --> pipeline
  profile --> pipeline
  platform --> pipeline
  session --> pipeline
  contract --> pipeline
~~~

禁止边（必须由 include-firewall 测试锁定）：contract/memory/session/profile/support 不得 include
backend/platform/terminal/pipeline；backend 不得 include pipeline；platform 不得 include backend。
现状违例：ancillary→backend（ancillary_controller.hpp:5,51）、profile→backend（binary.cpp:3-4、
glkv3_parse.cpp:3-4）、profile→pipeline（glkv3_parse.cpp:5）、memory→profile（address_space.h:4）。

### 2.3 目标结构/绑定图

~~~mermaid
flowchart LR
  W["GLK1 v2 / GLKv3 bytes"] --> C["profile framing"]
  C --> D["profile::Document (sections[key -> raw u64 + presence])"]
  D --> P["platform::abi Schema/View"]
  D --> V["platform::vivo Schema/View"]
  D --> B["backend::cve_2026_43499::backend_profile Schema/View"]
  P --> K["冻结的 kernel_offsets/TargetProfile (attack ABI)"]
  V --> K
  B --> K
  V --> A["ancillary::apply(stage, ops, policies)"]
  B --> A
  A --> X["VrGuardPolicy / VrTaskTagPolicy (platform::vivo)"]
~~~

kernel_offsets 在目标态是「冻结 ABI 影子」：owner 权威是 platform/backend 的 Schema，物理结构由
composition 合并填充，供 8 个攻击函数按偏移读取。

### 2.4 common.h 去耦方案

1. 先加后删：为 18 个文件各加「按需 include」，保留 common.h 一轮；host + NDK 双绿后再删。
2. 每个文件的 include 由编译器驱动补齐：删除 #include "common.h" → host/Ndk 报缺 → 只补报缺的头；
   记录最终列表。需要特别核对的两条顺序约束：
   - memory/constants.hpp 必须早于任何会定义 PAGE_SIZE 的系统头（runtime.cpp:1 的既有注释）；
   - kernelsnitch/utils.h 提供 pr_info/pr_warning/pr_error/pr_success 与 __ARM/_GNU_SOURCE，
     用宏的文件必须有它（或改为经 support 暴露日志薄封装，未验证：是否有意把 pr_* 收敛到 support）。
3. 迁移期把「谁需要哪些」登记到 §5.4 表，删除前逐条复核；删除后新增 include_firewall_test（A2-5-5）
   防止回退。
4. common.h 里的 backend 头（route_api.hpp、accessors.hpp、runtime_struct_offsets.h）不得被
   中性文件继续传递；中性文件若需要 ABI 常量，应 include platform/abi.hpp 或 memory/target.h。

---

## 3. 传输/入口去绑定

### 3.1 今天绑定了什么

| 位置 | 绑定的东西 | 问题 |
|---|---|---|
| profile/entry.h | read_glk1_*(..., profile::kernel_offsets *out, ..., binary_profile::component_ids *ids) | 入口类型即 43499 transport；无法接第二个原语不同的 backend |
| profile/entry.cpp decode() | 直接调 binary_profile::parse/parse_v3，二者内部 bind_document 到 kernel_offsets | 绑定不可替换 |
| profile/binary.cpp | bind_document 硬编码 Cve2026_43499Schema/Cve2026_43284Schema；v2 writer 字段表 | profile→backend，容器知道 owner |
| profile/glkv3_parse.cpp | combined_schema() 硬编码两个 backend schema；terminal_from_token/backend_from_token 用 pipeline::*_name | profile→backend + profile→pipeline |
| main.cpp | 声明 profile::kernel_offsets decoded；ids.middleware = decoded.route_kind() fallback；把 decoded 传给 run_orchestrated_pipeline | 入口/组合根认 43499 transport 与 route 细节 |
| pipeline/orchestrator.hpp、pipeline/pipeline.hpp、pipeline/backend_contract.hpp | 签名带 const profile::kernel_offsets &decoded | 组合层泄漏 backend 私有类型（ADR-0004 F1） |
| backend/*_backend.hpp、bootstrap.* | run(CoreSession&, const kernel_offsets&, ...)；install_profile(decoded) 写 TargetProfile | backend 从外部拿 transport，而非自己 bind document |

### 3.2 中性接口（目标）

以 profile::Document（已含 release/terminal/backend/middleware/steps + sections）作为唯一传输产物：

~~~cpp
// profile/entry.h —— 只做 framing，不 bind owner
struct ReadResult final {
    profile::Document document;
    int32_t error = 0;      // 0 = ok；非 0 与今日 errno 语义一致
};
ReadResult read_glk1_stdin();          // 无 out 参数
ReadResult read_glk1_frame_stdin();
ReadResult read_glk1_file(const char *path);
~~~

~~~cpp
// pipeline：编排层不再出现 backend 私有类型
template <class Backend, class Terminal>
struct Pipeline final {
    static RunResult run(session::CoreSession &,
                         const profile::Document &document,
                         const char *debug_dir, bool force_attack);
};
// backend 合约：状态构造即绑定点（ADR-0004 S8）
template <class B, class Input>
concept BackendExecution = /* ... */ requires(session::CoreSession &s,
        const profile::Document &doc, const char *dd, bool force, Input &in) {
    { B::state_from(s, doc) } -> std::same_as<profile::BindStatus>;   // 新增
    { B::run(s, dd, force, in) } -> std::same_as<session::StageResult>;
};
~~~

- main.cpp：auto result = profile_entry::read_glk1_*(); → 由 document.terminal/backend/steps
  构造 pipeline::ComponentSelection；不再读/写 decoded.route 或 route_kind()。route 作为
  document.middleware 交给 backend 解释（wire 字节不变）。
- Pipeline::run：先 Backend::state_from(session, document)（在 PI 窗口外，E1 → Rejected），
  再 Backend::run(...)，终止仍是 Terminal::run。
- 43499 的 state_from 实现在 backend/cve_2026_43499/backend_profile/bind.cpp：bind_all
  platform+backend schema → 填 Cve2026_43499State.profile + steps；保留 install_profile
  的 uname_r 校验与日志文本/顺序（A 级不变量）。
- 43284 的 state_from：bind 自己的 backend.cve_2026_43284 schema；无 route。
- binary_profile::component_ids 可保留为内部中间量或删除（未验证：main 之外是否还有消费者；
  目前仅 entry/main/orchestrator 使用）。

### 3.3 组合绑定与「冻结影子」

- 新增 profile::bind_all<SchemaList>(document, views...)（profile/schema.hpp）：对并集做
  UnknownSection/UnknownKey/Presence/Width 校验，再分派到各 owner View。解决「多 owner 共享 section、
  各自 bind 会互相判未知」的问题（ADR-0003 决策 4/5）。
- kernel_offsets 保持物理冻结：platform/backend 的 View 各自独立；composition 把 platform View 的
  平台字段显式合并进 kernel_offsets（一段有测试的 shim，见 §7.1 I-4）。这是本计划对 ADR-0003
  「每字段一个 owner + 物理结构冻结」两约束的调和点；若维护者要求彻底物理分离，转入 R-1 的延后批次。

---

## 4. A2-4 逐文件改动

> 依赖边 列给出 R1 影响；构建/测试 列给出需同步项。所有 git mv + 路径/命名空间/include 同提交。

### A2-4-1 中性 ancillary 机制

| 旧路径 | 新路径 | namespace | 依赖边变化 | 构建/测试 |
|---|---|---|---|---|
| ancillary/ancillary_policy.hpp | ancillary/ancillary_policy.hpp（改） | ghostlock::ancillary | 去 backend/*；留 contract/memory/support | ancillary_test 改包含/概念断言 |
| ancillary/ancillary_controller.hpp | ancillary/controller.hpp | ghostlock::ancillary | 删 backend/cve_2026_43499_state.hpp、ancillary/vr_*.hpp；template 接受 PolicyList + gate 仿函数 | tests/host/ancillary_stub.cpp 重组；steps.cpp 调用点 |
| ancillary/ancillary_controller.cpp | ancillary/controller.cpp（或删） | 同上 | — | Makefile CXX_SRCS |
| ancillary/vr_guard.hpp/.cpp | platform/vivo/vr_guard.hpp/.cpp | ghostlock::platform::vivo | 去 ancillary 依赖；依赖 platform::abi 值与写原语句柄 | 测试迁 platform_vivo_test |
| ancillary/vr_task_tag.hpp/.cpp | platform/vivo/vr_task_tag.hpp/.cpp | 同上 | 同上 | 同上 |

中性机制形状（建议，未验证：写原语句柄放 contract 还是 ancillary）：
- AncillaryStage { PreSpawn, PostSpawn, PreTerminal }（现 PreHandoff 更名 PreTerminal，需同步 R4/ADR-0004 用语；若坚持 R4 文字则保留 PreHandoff）。
- 行为契约：template<class P, class Ops, class Applicable> concept AncillaryPolicyFor，P::apply(stage, Ops&)；
  enabled/applicable 由调用方传入的 gate 决定（不再让 ancillary 认识 TargetProfile）。
- 写原语：保留 AncillaryZeroFn（Status(*)(uintptr_t,const char*)）以不新增 attack_write 调用点，
  位置可留 ancillary_policy.hpp 或入 contract（若入 contract，contract→memory 已允许，无新边）。

### A2-4-2 厂商行为下放 platform::vivo + 注册注入

| 文件 | 改动 | 依赖边 | 构建/测试 |
|---|---|---|---|
| platform/vivo/schema.hpp（新） | platform::vivo::Schema/View：meta.vr_guard、offset.vr_sys_exit_tp、vr_guard.tracepoint_funcs | platform→contract,memory,profile,ancillary,support | profile_manifest_test 增 owner；GLKv3 表增 platform.vivo 段（A2-6 命名见 ADR A6） |
| platform/vivo/vr_guard.hpp/.cpp | 从 ancillary 迁；enabled(View)；写经注入句柄 | — | ancillary_test 拆出 vivo 段 |
| platform/vivo/vr_task_tag.hpp/.cpp | 同上；VR_TAG_B_OFF 来自 platform/vivo/macros.h | — | 同上 |
| platform/vivo/macros.h（迁 profile/macros.h） | VR_TAG_B_OFF 默认 0x2c 不变 | — | ancillary_test/platform_vivo_test include |
| platform/vivo/registry.hpp（新） | using VivoAncillaryPolicies = std::tuple<VrGuardPolicy,VrTaskTagPolicy>; | platform→ancillary | — |
| backend/cve_2026_43499/steps.cpp | 两处 AncillaryController<M>::apply → 传 VivoAncillaryPolicies{} + gate（读 state 的平台 View）+ AncillaryZeroFn；语句/日志顺序不变 | backend→platform,ancillary（已允许） | host 数据流 harness 需 fake vivo 行为 |
| backend/cve_2026_43499/schema.hpp/glkv3_schema.hpp | 删除 meta.vr_guard、offset.vr_sys_exit_tp、vr_guard.* 三处声明（迁 platform::vivo） | — | manifest/v3-manifest 重生成 |
| profile/model.h | vr 字段物理保留（冻结）；访问器 vr_guard_*() 可留作过渡，但 owner 改为 platform::vivo View | — | owner_schema_test 改 |

> 注意：meta.vr_guard 与 backend 的 meta.{kernel_major,fallback_route,safe_mode} 同 section 不同 key，
> 属 ADR-0003 允许的「共享 section、互斥 key」，必须走 bind_all。

### A2-4-3 platform::abi owner schema/view（逻辑归属，物理冻结）

| 文件 | 改动 | 依赖边 | 构建/测试 |
|---|---|---|---|
| platform/abi.hpp（新） | platform::abi::Schema/View：task_struct.*、cred.{usage_offset,caps_offset,ref_count,ref0..3_offset}（layout）、offset.{init_task,init_cred,empty_zero_page,root_task_group,selinux_enforcing,selinux_blob_sizes,security_hook_heads}、kernel.{kernel_phys_load,kernel_phys_offset} | platform→profile,contract,memory | profile_manifest_test owner=platform::abi |
| backend/cve_2026_43499/schema.hpp/glkv3_schema.hpp | 删除上述 platform key；保留 cred.{copy_size,usage_value,caps_count,caps_value,ref0..3_image}、offset.slide_*、kernel.{compact_waiter,kernelsnitch_collisions,mm_struct_sz}、meta.{kernel_major,fallback_route,safe_mode}、execution.*、route.*、backend.cve_2026_43499.steps | backend→platform（用平台 View，不重复声明） | owner_schema_test/glkv3_schema_test |
| profile/model.h | 字段物理保留；static_assert(sizeof) 不变 | — | session_layout_test |
| composition（pipeline 或 backend_profile/bind.cpp） | bind_all + 平台 View→kernel_offsets 合并 shim | backend→platform | 新增 host 测试 |
| memory/target.h（可选，A2-4-3b） | 拆分：ABI/phys 常量归 platform::abi 常量面；FAKE_WAITER_*/FAKE_TASK_*/payload::* 归 backend；memory 只留 DIRECT_MAP_*/P0_*/struct page/KernelAddress（ADR-0001 §15、R6） | 影响面大 | target_constants_test |

### A2-4-4 profile 类型物理迁移（延后/条件批次）

| 旧路径 | 新路径 | namespace | 说明 |
|---|---|---|---|
| profile/model.h（业务部分） | backend/cve_2026_43499/backend_profile/model.hpp | ghostlock::backend::cve_2026_43499::backend_profile | execution_settings/RouteKind/kRouteCatalog/kernel_offsets/TargetProfile/typed layouts |
| profile/accessors.hpp | backend/cve_2026_43499/backend_profile/accessors.hpp | 同上 | slide 访问器 |
| profile/runtime_struct_offsets.h | 拆 platform/abi（ABI 偏移）+ backend（slide/fake） | — | 见 A2-4-3 |

阻塞：memory/address_space.h:4,27,38,46 以 profile::TargetProfile 为参数；memory→backend
非法。必须先做 A2-4-5。

### A2-4-5 memory::AddressSpace 解耦 TargetProfile（A2-4-4 前置）

| 文件 | 改动 |
|---|---|
| memory/address_space.h/.cpp | init_for_soc/init/soc_name 参数从 const TargetProfile* 改为中性 POD memory::LaunchGeometry（uname_r、kernel_phys_load、kernel_phys_offset、init_cred_offset） |
| backend/cve_2026_43499/bootstrap.cpp | 组装 LaunchGeometry 后调用；日志/顺序不变 |
| memory/address_space.h | 删除 #include "profile/model.h"（破坏 memory→profile 边） |
| 测试 | address_space_test 改用 POD；新增 memory→profile 禁止断言（A2-5-5 防火墙） |

---

## 5. A2-5 逐文件改动

### A2-5-1 entry 产出中性 Document

| 文件 | 改动 | 依赖边 | 测试 |
|---|---|---|---|
| profile/entry.h/.cpp | read_glk1_* 返回 profile::Document + 错误码；decode() 只 framing；修正「v3」注释为 v2/v3 | 去 binary_profile::parse 绑定 | profile_entry_test 改 |
| profile/binary.h/.cpp | parse/parse_v3 拆出 framing-only 版本（产 Document）；bind_document 迁 backend（A2-5-2） | 去 backend/*schema | profile_binary_test/document_schema_test |
| main.cpp | 用 Document 构造 ComponentSelection；删除 decoded.route/route_kind() fallback 绑定 | 去 kernel_offsets | — |

### A2-5-2 pipeline/backend 接口去 kernel_offsets

| 文件 | 改动 |
|---|---|
| pipeline/backend_contract.hpp | BackendExecution 用 const profile::Document&；新增 B::state_from(CoreSession&, const Document&) |
| pipeline/pipeline.hpp | Pipeline<B,T>::run(CoreSession&, const Document&, const char*, bool)；先 state_from |
| pipeline/orchestrator.hpp | 签名改 Document |
| backend/cve_2026_43499_backend.hpp/.cpp | run(CoreSession&, const char*, bool, RootedChild&)；新增 state_from；route 从 state 读 |
| backend/cve_2026_43284_backend.hpp/.cpp、backend_terminal.*、stage_runner.* | 同步 Document 化（43284 只读自己的 View） |
| backend/cve_2026_43499/bootstrap.cpp | install_profile(decoded) → install_document(document)（行为/日志不变） |
| backend/cve_2026_43499/backend_profile/bind.cpp（新） | bind_all + 合并 shim；v2/v3 共用 |
| 测试 | backend_contract_test、component_catalog_test、host/backend_dataflow_test、profile_v3_test、profile_entry_test |

### A2-5-3 GLKv3 解码去 backend/pipeline

| 文件 | 改动 |
|---|---|
| profile/glkv3_parse.hpp/.cpp | 删除 backend/*glkv3_schema.hpp 与 pipeline/component_catalog.hpp；改为接受调用方传入的 Schema/id 映射（模板或注册头） |
| pipeline/profile_registry.hpp（新，或 backend_profile/registry） | 组合 root + platform::abi + platform::vivo + backend schema；token↔id 映射（pipeline 可达 contract/backend/profile 类型） |
| profile/glkv3.hpp | 容器 codec 保持中性；若需类型化 raw 值，扩展 profile::Value 携带 wire type（未验证：是否必要，取决于是否允许 entry 不做 owner 校验） |
| 测试 | glkv3_schema_test、profile_manifest_v3_test、Kotlin ProfileManifestV3AgreementTest |

### A2-5-4 common.h 逐文件去耦

见 §2.4；逐文件 include 初表：

| 文件 | 去 common.h 后需要的头（初判，编译驱动补齐） |
|---|---|
| main.cpp | profile/entry.h、pipeline/orchestrator.hpp、support/cli.hpp、support/fatal_error.hpp、support/run_state.hpp、系统头 |
| session/runtime_config.cpp | session/runtime_config.h、memory/target.h（若用常量）、kernelsnitch/utils.h（日志） |
| race/threads.cpp | race/pi_race.h、session/core_session.hpp、backend/cve_2026_43499_state.hpp、support/run_state.hpp、memory/target.h |
| race/threads.hpp | 仅前向所需（未验证，可能只需 race/pi_race.h） |
| support/util.cpp | support/decls.hpp、support/time.h、memory/heap_context.h、memory/payload_builder.h、memory/address_space.h、profile/accessors.hpp、backend/cve_2026_43499_state.hpp、kernelsnitch/utils.h |
| backend/cve_2026_43499_backend.cpp | platform/runtime.hpp、backend/cve_2026_43499_state.hpp、session/runtime_config.h、terminal/*、memory/target.h、support/decls.hpp |
| backend/cve_2026_43499/steps.cpp | ancillary/controller.hpp、platform/vivo/registry.hpp、backend/victim/victim_process.hpp、support/run_state.hpp、memory/direct_map.hpp、memory/target.h |
| backend/victim/victim_process.hpp/.cpp | backend/victim/victim_context.hpp、系统头、kernelsnitch/utils.h |
| backend/cve_2026_43499/primitives.cpp | memory/*、support/decls.hpp、backend/cve_2026_43499_state.hpp |
| backend/cve_2026_43499/bootstrap.cpp | session/runtime_config.h、memory/address_space.h、profile/*（过渡）、support/fatal_error.hpp |
| terminal/root_script.cpp | terminal/root_script.hpp、session/runtime_config.h、support/native_resource.hpp、日志 |
| terminal/root_child.cpp | terminal/root_child.hpp、terminal/handoff_probe.hpp、session/runtime_config.h、support/* |
| route 三个 .cpp | memory/target.h、memory/payload_builder.h、race/pi_race.h、support/*、backend/cve_2026_43499_state.hpp、日志 |
| ancillary/vr_guard.cpp/vr_task_tag.cpp | 迁 platform/vivo 后重列（见 A2-4-2） |

### A2-5-5 防火墙与 fake backend 测试（R8）

| 文件 | 内容 |
|---|---|
| tests/include_firewall_test.cpp（新） | 静态扫描/编译断言：中性头（contract/memory/session/profile/support + pipeline?）不得 include backend/platform/terminal/pipeline；backend 不得 include pipeline |
| tests/fake_backend_stub.hpp（新） | 原语/route/State 都不同的第二 backend，编进 host，证明可组合（R8） |
| Makefile | NATIVE_HOST_TESTS 增两项；HOST_ATTACK_DATAFLOW_SOURCES 调整 |

---

## 6. 分批与门禁

### 6.1 批次顺序（依赖）

~~~mermaid
flowchart LR
  A1["A2-4-1 ancillary 中性"] --> A2["A2-4-2 vivo 下放"]
  A2 --> A3["A2-4-3 platform::abi schema"]
  A3 --> A4["A2-4-4 profile 类型迁移 (延后)"]
  A5["A2-4-5 memory 解耦"] --> A4
  A1 --> S1["A2-5-1 entry->Document"]
  S1 --> S2["A2-5-2 pipeline/backend 去 kernel_offsets"]
  S2 --> S3["A2-5-3 glkv3 去 backend"]
  S4["A2-5-4 common.h 去耦"] --> S5["A2-5-5 防火墙/fake backend"]
  A4 --> S2
  S3 --> S5
~~~

### 6.2 每批门禁与 cmp 判定

| 批次 | host | NDK | lint | cmp_disasm | 真机 | 43499 机器码理论预期 | 需 worktree 对照 |
|---|---|---|---|---|---|---|---|
| A2-4-1 | 全绿 | 零告警 | 0 | 必须 | 可选 | 8 函数指令形状不变（ancillary 调用不在 8 函数内；可能 LTO 地址/reloc 差 → --reviewed） | 建议 |
| A2-4-2 | 全绿 | 零告警 | 0 | 必须 | 建议（vivo 6.1 才验证 vr） | 同上（厂商写仍在 W1/W2，不改 do_one_write） | 建议 |
| A2-4-3 | 全绿 | 零告警 | 0 | 必须 | 必须（Xperia 5.15 单 route） | 同上；绑定在启动期 | 必须 |
| A2-4-3b | 全绿 | 零告警 | 0 | 必须 | 必须 | 同上（常量值不变） | 必须 |
| A2-4-4 | 全绿 | 零告警 | 0 | 必须（新 TARGETS/基线） | 必须 | 允许形状变化；类型改名不改布局，但 LTO/符号面变，逐条复核 | 必须 |
| A2-4-5 | 全绿 | 零告警 | 0 | 必须 | 必须 | 期望不变（仅 setup 代码） | 建议 |
| A2-5-1 | 全绿 | 零告警 | 0 | 必须 | 必须 | 期望不变（启动期） | 必须 |
| A2-5-2 | 全绿 | 零告警 | 0 | 必须 | 必须 | 期望不变；do_one_write 不得受影响 | 必须 |
| A2-5-3 | 全绿 | 零告警 | 0 | 必须 | 必须 | 期望不变 | 必须 |
| A2-5-4 | 全绿 | 零告警 | 0 | 必须 | 必须 | 理论上不变；include 顺序风险最高 | 必须 |
| A2-5-5 | 全绿 | 零告警 | 0 | 必须 | 可选 | 仅测试 | — |

- 理论上不应改 43499 机器码的批次：A2-4-1/2/3/3b/5、A2-5-1/2/3/4。这些批次必须用
  git worktree add 在批前 commit 建隔离树，各自构建 build/native/ghostlock，跑
  python3 tools/cmp_disasm.py <批前二进制> <批后二进制>；任何未复核差异即停批调查（AGENTS 门禁纪律）。
- 允许差异的批次：A2-4-4（符号/地址面变化），需维护者复核并更新 TARGETS 候选与基线。
- 真机门禁按 docs/analysis/device-gates/ 模板归档；vivo 厂商行为最终需 vivo 6.1 真机（本计划
  A2-4-2 的 vr 行为变更无法在 A301SO 上验证）。
- 攻击路径改动（A2-4-1 起都触及 W1/W2 调用点）按 AGENTS §核心攻击代码审查附「所有权/UAF/终结点顺序表」。

---

## 7. 不变量与风险

### 7.1 不变量（必须保持）

- I-1 43499 8 函数（owner_thread/waiter_thread/consumer_thread/run_main_route_threads/
  do_kernel5_fake_lock_route/do_one_write×3）指令形状与相对顺序；未复核差异不得合入。
- I-2 sizeof(kernel_offsets)==520、alignof==8、sizeof(TargetProfile)==784、alignof==8；
  session_layout_test、kernel_offsets 偏移不变（CoreSession.backend_state 起始 104）。
- I-3 GLKv3 golden 字节不变（wire 不变）；v2 只读；Kotlin↔native 键名/顺序/manifest 一致。
- I-4 每个 (section,key) 恰一个 owner（ADR-0003 R5）；kernel_offsets 合并 shim 是唯一的
  跨 owner 机械映射，须有 host 测试锁定「平台 View 值 == kernel_offsets 对应成员」。
- I-5 W1→W2→W3→terminal 顺序、ancillary PreSpawn/PostSpawn/PreTerminal 时机、
  attack_write 调用点数量与 adapter 不变。
- I-6 仅 g_exploit_session 一个可变全局；无新全局。
- I-7 R1 依赖边；新增 include-firewall 测试锁定。

### 7.2 风险与缓解

| 风险 | 说明 | 缓解 |
|---|---|---|
| R-1（最大） | profile::kernel_offsets/TargetProfile 物理迁 backend 撞 memory→profile，违反 R1；强做会改布局并可能改 8 函数 | 先 A2-4-5 解耦（LaunchGeometry POD），再 A2-4-4；A2-4-4 单列、全量重跑 cmp + 真机；未验证 是否必须在 A2-4 内完成 |
| R-2 | 多 owner 共享 section 的 strict bind 互相判未知 | bind_all<SchemaList> + 跨 Schema 查重测试 |
| R-3 | 平台 View→kernel_offsets 合并成为「第二字段映射」 | 限定为机械 shim + host 等价测试；长期若攻击码迁到平台 View 则删除 |
| R-4 | common.h 删除遗漏 include；顺序（PAGE_SIZE、pr_* 宏） | 先加后删、编译驱动、逐文件记录；include-firewall 防回退 |
| R-5 | 循环依赖：profile/binary 绑定 backend、ancillary 读 backend state、glkv3_parse 引 pipeline | 归位（绑定上移 backend/pipeline）；CI 防火墙测试 |
| R-6 | AncillaryContext 退役后写原语句柄形态改动波及攻击函数 | 保留 AncillaryZeroFn 签名与调用点；cmp 判定 |
| R-7 | 真机不可验证 vivo 行为（设备是 A301SO） | A2-4-2 标注「vivo 6.1 未验证」；不宣称 supported |
| R-8 | KERNEL-PANIC-01 环境抖动被误判为回归 | 同构建复现 + 冷机复跑，遵 §8.4 归因纪律 |

### 7.3 回滚

- 单分支、按批提交；每批一个 commit，回滚 = revert 该 commit（或 git worktree 隔离树）。
- A2-4-4 / A2-5-2 若 cmp 不可复核：回滚到批前，保持 kernel_offsets 冻结，仅保留 A2-4-1/2/3 与
  A2-5-1 的归位成果（不搬类型、entry 可暂保留过渡重载）。
- common.h 删除单独一个 commit，便于整段恢复。
- 无 wire/GLKv3 变更，无需回滚 Kotlin/资产。

---

## 8. 未验证 / 开放问题

1. 未验证：A2-4 是否要求 kernel_offsets/TargetProfile 的物理迁移，还是只要求 owner
   schema 归属；ADR 与 branch-plan 的文字（物理抽离并入 A2-4）与本计划的 byte-identity 约束存在张力，
   需维护者裁决。
2. 未验证：platform::abi 的常量面（memory/target.h 拆分）是否在 A2-4 范围；拆分粒度与
   memory/target_constants.hpp 的归属。
3. 未验证：GLKv3 容器是否允许「不做 owner 校验」的 Document（决定是否需要给 profile::Value
   增加 wire type）。
4. 未验证：AncillaryStage::PreHandoff 是否更名 PreTerminal（R4 与 ADR-0004 S4/A5 用语）。
5. 未验证：组件身份词汇（BackendKind/TerminalKind 与 binary_profile::k* 常量）何时收敛到
   contract（ADR-0004 S2），本计划不动。
6. 未验证：platform/device_facts 是否在后续归入 platform::runtime；本计划不搬。

---

## 9. 建议的第一批

A2-4-1（中性 ancillary 机制），理由：
- 不碰 wire、不碰 kernel_offsets 布局、不动 8 个攻击函数体；只改 src/core/ancillary/* 与
  backend/cve_2026_43499/steps.cpp 两个调用点。
- host（ancillary_test + 数据流 harness）、NDK、lint、cmp 四项可判定；git worktree 对照成本低。
- 它同时清掉一条明确违例（ancillary→backend），是 A2-4-2 的直接前置；即便后续批次暂缓，该批
  也独立成立。

次选并行批：A2-5-4（common.h 去耦）与主线无耦合，可在 A2-4-1 门禁运行期间由另一子智能体
以独立 worktree 推进（写范围不重叠：18 个文件 vs ancillary/*+steps）。

---

## 10. 变更记录

- 2026-10-04：建立 A2-4/A2-5 逐文件计划；给出批次、门禁、cmp 判定与回滚；识别最大风险 R-1。
