# AGENTS.md

GhostLock：Android 内核提权工具。通过 PI-futex 竞争覆盖 waiter，依次完成
W1（SELinux）、W2（凭据/uid 0）、W3（seccomp），最后 handoff 给 root child 做
KernelSU 模块加载。内核按精确 `uname -r` 匹配 HOCON profile，未匹配即拒绝运行。

## 三层架构

| 层        | 位置                | 说明                                                                                       |
| --------- | ------------------- | ------------------------------------------------------------------------------------------ |
| Android   | `app/`              | Kotlin/Compose UI、HOCON profile 解析/合并/覆盖、Shizuku UserService、日志                 |
| Native    | `src/core/`         | C++23 攻击 runtime，产物 `build/native/ghostlock`                                          |
| Extractor | `tools/extract_rs/` | Rust；boot.img / OTA / URL → `--format conf`（flatten GLK profile）/ `--format json`（v1） |

- Native 不是 JNI：`libghostlock.so` 是可执行 ELF，由 Kotlin `ProcessBuilder` 启动。
  CLI（S4 R2b 后仅 7 项，**选择与策略不得出现在 CLI**）：`--ghostlock-app-call`（stdin 读长度前缀的 GLKv3 文档，其后可接运行时密钥会话帧）/
  `--load-prebuilt-profile <bin>` / `--enable-status-record` / `--dump-kernel-log <dir>` / `--force-attack` /
  `--allow-dev-target`（**只放宽绑定期 carrier 校验**，链内仍拒 dev 路径） / `--probe-cve-2026-43284 <ko>`（只读诊断）/
  ~~`--plugin-probe <path.so> [--expect-sha256 <hex>]`~~（只读插件描述，不注册、不运行 hook；**已按用户指令字面注释**）。
  **⏸ 插件与 payload 工程仍按用户指令暂停（2026-10-05）；自定义 handoff 已于 2026-10-06 解冻并纳入 `available`（D1–D4 已批准）**：插件的**运行时接线**（`main.cpp` 的构造/open/bind/close、`execution_binding.cpp` 的 sink 绑定）、**`--plugin-probe` 入口**、以及 **payload owner**（`glkv3_parse.cpp` 的校验分支与 owner 名单、`schema.hpp` 的 `kPayloadGlkv3Fields`、manifest 8 行、相关测试与 Makefile 目标）**全部字面注释**；⇒ **`plugin`/`payload` 段现在出现即拒（fail-closed）**；App 侧隐藏两个入口且不再发射。代码与测试保留，**恢复＝撤销注释 + 跑门禁**（恢复清单见 `docs/archive/20261007-2237-branch-plan.md`）。
  插件 **P1 已落地**：导入（no-backup `countermeasures/` + 本地 SHA-256 + 探针）→ 校验（描述符驱动的 `params.*`）→ 发射（仅 `enabled=true` 写 `plugin.<id>.*`，文档里出现 `enabled=false` 一律拒绝）。**运行时「加载 → 按 stage 调用 → 卸载」已接线（step 3a）**：组合根构造 `PluginHost` 并**仅**在 43284 的 bind 前 `open(WindowState::WaiterClosed)`（**43499 的 `pre_terminal` 待 step 3b**；R1：PI waiter 存活期不得 open），LKM 驻留窗口内经中性 `PluginStageSink` 派发 `POST_TERMINAL`（fail-soft），pipeline 之后 `close()`，诊断仅 `registered() > 0` 时打印（无插件零新增字节）。`src/core/pipeline/**` 仍对插件宿主零引用——**这是设计如此**（能力点不在组合/分派层，而在组合根与 backend 窗口），不是「未接线」；见 `docs/archive/20261007-2237-branch-plan.md` task-9。
  staged 入口（`--run-cve-2026-43284`/`--stage`）与 `--plugin`、`--cve43284-*`、`--allow-vermagic-rewrite` 已删除（dev 走同一文档 + 同一 Pipeline）；
  无参数的 v1 `offsets.json` 入口已移除；入口细节见 `docs/analysis/native-entrypoint-plan.md`（git 历史）与 `docs/analysis/device-gates/s4-r2b-20261005-pass.md`。
- 内置 profile 在 `app/src/main/assets/profile/`：`index.conf` 索引、
  `<uname-r>.conf` 每 release 一份、`execution-*.conf` 公共/分 route 调参、
  `credential-6x.conf`、`kernelsnitch-6x.conf`。格式为 HOCON（支持 `include`）。
- 组件模型（ADR-0004 + **ADR-0006**）：**① 组合权威仍是 `contract::kCombinationCatalog`**——它是组合的
  **唯一权威**（token → backend/route/steps/terminal/available），用于**内部归一化键 + `supported` 判定 + dispatch 依据**；
  `Pipeline` 按（归一化后的）组合**编译期固定**并逐组合 static_assert。**② 用户选择面 = `backend.<id>{ route, queue }`（步骤队列）**——
  `available` 的对象形态声明 `<backend>{ route=<str>, queue=[ 对象元素 ], experimental=<bool> }`（设计稿已归档，索引见 docs/archive/README.md；契约 §3.20）；
  **token 形态已删（M5 = `e59a8479`）⇒ 出现即拒**：HOCON 的**列表形态**（token 列表）**出现即拒**（两条独立具名诊断：`the token-list form was removed in M5; declare route+queue` / `empty token list is not a selection; declare route+queue`），native 侧具名拒 token（`plan_error reason=token-form-removed path=backend.<id>.steps hint=declare-route-and-queue`）且**归一化不再物化 token**；App **停发 wire `steps`**；**沿革**：`backend.<id>.steps` 曾是迁移期语法糖，M5 已删；**不得再读成「token 是用户选择面」**。
  **沿革（不删历史）**：2026-10-05「选择由 token 白名单表达」→ **2026-10-06「队列取代 token」**（理由：**HOCON 可读性**；用户裁决）。
  **对拍物证**：`ProfileLayoutAvailableTest` 等 + `profile-core/src/test/resources/glkv3-native-fixture.tsv`（**91 行**，native `--dump-fixture`）
  + Kotlin golden（**3964 字符**）与 `make -C src glkv3-golden-hex` **逐字符相同**；本片提交 = **`4c20142f`**。
  当前**已接线**：`cve_2026_43499 × {mcast,pselect,tcp}_{rootchild,shizuku}`
  与 `cve_2026_43284 × umh`；**计划项**（`available=false`）：`{mcast,pselect,tcp}_umh`、`rootchild`、`shizuku`
  （解析接受、选择门禁拒绝、UI 置灰）。**terminal 归属见 ADR-0006**：词汇（rootchild/shizuku/umh）保留，
  **实现下放 backend**，共享件（脚本生成/探测/UMH 命令/输入结构）中性。
  每轴可用性：backend 为 `cve_2026_43499` 与 `cve_2026_43284`；`cve_2026_64560`/`cve_2026_31431`/`cve_2026_43503`/`cve_2026_23274` 为纯头占位不可用；
  terminal 为 `root_child` 与 `umh_forward`（均已可用）；旧文档的 `file_write`/`panic` 占位**不存在**。
  route（`select_stack`/`tcp_zerocopy`/`multicast_waiter`）是 backend 内部策略，按 profile 选，不是装配轴；`platform`/`plugin` 为横切。
- 契约/组合分工：`contract/identity.hpp` 定义 kind、每轴可用性谓词、identity 声明与执行概念（host 可编译），
  并且是**组合（wiring）的唯一权威**——`kCombinationCatalog`（token → backend/route/steps/terminal/available）；
  `pipeline/component_catalog.hpp` 只**按 token 分派**（`DispatchTarget`、`combination_supported`/`dispatch_target_of`），
  `pipeline/orchestrator.hpp` 逐 case 用 `Pipeline::target` static_assert 锁定；导出与 Kotlin 对拍见文档约定。
  `selection_supported()`（设备已核实）与 `combination_supported()`（已接线）是两个不同问题；选择显式来自 profile/wire，不从 kernel 版本推断。
- 顶层 owner 白名单（native `known_owner_section`，`src/core/profile/glkv3_parse.cpp`）：现为 **`backend.<id>`** 与 **`platform.*`**（后者正按 HOCON 重构**迁移**到 `backend.cve_2026_43499.abi.*`，落地后 `platform` owner 一并删除）；**`common` 与 `countermeasure` 已删除（出现即拒）**；**`plugin` 与 `payload` 按用户指令注释掉（出现即拒）**；新增 owner 必须同批更新本节、manifest 与 `docs/development/full-process-uml.md`。
- **根级标量通道**（HOCON 重构引入）：`schema` / `release` / **`kernel_major`** / **`kernel_minor`（可缺省）** / **`safe_mode`** 位于 wire 根 map，走**特例通道**（不是段机制）；在 `profile-manifest-v3.tsv` 里以 **owner = `root`、path = 裸键名** 单列（`root\tkernel_major\tuint\t0\t-\tprofile\t-`）。实现上用**空段名「根段」**承载（`document.hpp` 的 `kRootSection`），使 owner bind 的**唯一取值路径** `find_value(section,key)` 与根键同构——**这样两处按 section 拷贝的过滤副本不会漏拷**；**若改用具名成员，漏一处即静默 `kernel_major=0`，勿改**。
- **`vr_guard` 分两期**：**(b) profile 面已删（`b55708a8`）** —— `common.vr_guard` + `countermeasure.vivo_vr_guard.tracepoint_funcs` 消失 ⇒ 字段无写入者 ⇒ `vr_guard_enabled()` 恒 false ⇒ `backend/cve_2026_43499/steps.cpp` 两处 `VivoPluginPolicies::apply` **可证明 no-op**、攻击路径不变；**(a) 彻底删除 vivo 代码**（`platform/vivo/**` + 两处 include/调用 + 测试）= **攻击路径改动 ⇒ 必须真机门禁**，挂账待设备。

## 常用命令

```sh
# Native
make -C src ghostlock                 # NDK 构建（ANDROID_NDK_HOME 或 local.properties）
make -C src native-host-tests         # 主机单元测试（含固定向量/生命周期）
make -C src lint-tidy                 # clang-tidy，要求 0 findings
ANDROID_NDK_HOME=... make -C src      # NDK 未自动探测时的显式写法

# Android / 提取器
./gradlew :app:assembleDebug
./gradlew :app:testDebugUnitTest
./gradlew exportProfiles        # 生成 GLKv3 .bin 到 build/profile/
(cd tools/extract_rs && cargo test --release)

# 攻击函数形状对比（**可选诊断**，不再是门槛；基线 build/native/ghostlock-B0）
# 攻击路径改动的判据是**真机门禁**，不是反汇编一致性。
python3 tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock
```

## 最高思想与质疑义务（2026-10-07 用户裁定 · 强制）

**`docs/development/软件工程守则.md`（《软件工程教程》整理版，6018 行）是本项目的最高思想**，效力高于本文件及其它一切文档、计划、规范、约定与个人偏好。

- **层级**：守则 = 最高思想。**本文件（AGENTS.md）与其它文档（`engineering-standards.md`、`design-philosophy.md`、`engineering-rules.md`、各 `plan/`、各 `analysis/`）内的一切规则、标准、设计、经验，只能作为「经验条目」**
  —— 可补充、可细化，**不得与最高思想冲突、不得放宽、不得绕过、不得并列**。冲突时**以守则为准**，被冲突的经验条目视为**待修订**。
- **当责（质疑—拒绝—教育）**：任何人（用户 / 开发者 / Lead / 子智能体）提出的**要求、设计、决策、流程改动或流程打断**，若
  ① 违反守则任何条文；或
  ② 破坏已建立的结构约束（阶段门、单变量、一处读取、单一权威、临时物零容忍、真机判据、产物身份核验等）；或
  ③ **打断开发—设计—规划流程**（越过阶段门推进、无判据即继续、多线并行、以修改判据来通过门禁、以临时旁路替代设计）
  ⇒ **必须当场质疑、拒绝执行，并教育提出者**：引用守则条文 + 项目物证（失败记录、指纹、判据），说明**冲突点与代价**，并给出**合规替代方案**。
  **不得沉默服从，不得阳奉阴违，不得"先做了再说"。**
- **唯一例外 · 强制确认**：用户**明确、知情地强制要求**越过守则（并说明接受后果）⇒ 可以执行，但必须同批留下**沿革记录**
  （越过哪一条、谁确认、代价为何）并登记为**技术债**，约定偿还时机。**未经强制确认的越权一律拒绝。**
- **反向义务**：对**符合**守则的要求必须**高效执行**；不得以"守则"为借口拖延、扩范围或拒绝正常需求。
- **记录义务**：每次质疑 / 拒绝 / 教育 / 强制确认，留一行可检索记录（时间 · 条文 · 结论）。
- **沿革**：本条由用户 2026-10-07 裁定，**取代并强化**既有条款「用户提出与既有设计/决定冲突时，Lead 必须当场质疑并明确列出冲突」
  （旧：质疑后交用户裁决；新：**默认拒绝执行 + 教育提出者 + 仅在强制确认后放行**，且越权必留沿革与技术债）。

## 改动流程：先设计，后改动（强制）

先读 `docs/development/design-philosophy.md`（设计思想：11 条原则，以 SWEBOK/ISO 12207/ISO 25010
等软件工程理论为依据，含理论锚点与项目实例）。
完整工程规范见 `docs/development/engineering-standards.md`（架构/数据流/控制流/数据结构/代码风格/
文档/验证的详细规则 + 外部标准来源）。**任何改动前必须先完成设计**：

1. **Explore**：先读代码与文档；用 `git log --all -- <path>` 查历史设计与 device-gate 证据
   （现行计划见 `docs/plan/MASTER-PLAN.md`；历史计划在 `docs/archive/20261007-2237-*`）。
2. **Design**：S 级（注释/格式）直接改；M 级写清动机/影响文件/行为差异/验证计划；**L 级**
   （攻击关键路径、wire/profile 格式、跨 Native↔Kotlin 契约、公共数据结构、新增 route）
   必须先产出计划文档（模板见 `docs/development/documentation-standards.md`）并获用户认可，再写代码。
3. **Implement**：最小改动，只碰设计列出的文件；不动 `LegacyProfileConverter.kt` 的 v1 转换与
   规范中的"明确保留"清单。`kernelsnitch/` 上游已冻结，可在顶级架构重构中作为 backend 模块
   拆分/改写，但必须先有设计与测试，不得顺手改。
4. **Verify**：按级别跑满 §1.3 门槛并保留证据；汇报时给出命令与结果，不写"应该没问题"。

- 攻击路径改动 = **真机门禁 + 门禁记录**（格式见 `docs/development/documentation-standards.md`），缺一不可。
  `cmp_disasm` 是**可选诊断工具**：反汇编差异本身**不再阻塞**批次，真机测试通过即可。
- 大改动按批次推进，一个批次只做一类事，上一批验证通过再进下一批。
- **新实验必须从「生产路径」启动，不得靠新增 CLI 旗标/旁路逻辑来试（用户指令 2026-10-05）**：优先**预留扩展点/解除限制/参数化**，使实验＝**小改动或去掉一个限制**，直接在**生产路径**上跑；**禁止**为了试一个功能而临时加一堆可选 CLI 参数与旁路分支。本项目已吃过这个亏：`--cve43284-*`、`--plugin`、`--allow-vermagic-rewrite`、staged 入口等都是**先加后删**（AGENTS 已记载其删除），清理成本高且有「dev 路径与生产路径行为漂移」的风险。⇒ 任何**仅服务实验**的入口，必须在**同批**登记清理计划（谁在什么条件下删），默认视为技术债。

## 代码修改流程（注释套旧码 · 新码标注 · 标准化注释）（2026-10-07 用户指令 · 强制）

**核心要求：不得在原有代码上直接改掉它。** 修改流程固定为五步：

1. **套住旧码**：把要被修改的**原有代码用注释套住**（保留原文，作为参考）。
2. **打标记**：在被注释的旧码与待实现的新代码之间，标记 `//TODO(reason)` —— **reason 必须写清**（为什么改、改到什么状态算完成）。
3. **写新码**：在标记之后写新实现。
4. **确认完成**：以该批次的**验收判据全绿** + Lead/用户确认为准（判据见"验证门槛"）。
5. **删除旧码注释**：确认完成后**立即删除**被注释的旧代码块，并核对该文件无残留。

**有界性（与最高思想对齐）**：被注释的旧码属**临时物**，受《软件工程守则》§6.2 约束 ——
**同一批次内必须清零**，不得跨批次、不得进入基线；批次计划里须登记"哪几处旧码参考块待删"。

**标准化注释（新增即写；改旧码时顺手补）**：
- **Kotlin / Java / Gradle KTS**：**KDoc**（`/** … */`，含 `@param` / `@return` / `@throws`）。
- **C++ / C**：**Doxygen 风格**（`/** … */`，含 `@param` / `@return` / `@note`）；不得只写 `//` 碎片注释代替文档注释。
- **Rust**：**rustdoc**（`///`，含 `# Arguments` / `# Errors` 段）。
- **HOCON / shell / Makefile**：`#` 注释，同义表达。
- **最低内容**：目的（做什么/为什么）· 参数 · 返回 · 失败/错误语义 · 前提（时序/线程/生命周期，若相关）· 所属阶段或组件（若相关）。
- **触旧补注**：任何被本次改动触及的旧类/旧函数，**同批补上**上述标准注释（不得只改不注）。

**例外（须用户确认）**：
- **机械改动**（纯格式、重命名、错别字、注释本身）可直接改写，但**仍须**按标准补注释，并在 `//TODO(reason)` 里说明"机械改动"。
- **无法用注释套住**的情形（例如单行 token 常量修复、仅供工具识别的字符串）⇒ 用独立的 `//TODO(reason)` 标注该处，说明为何不能套码。

**沿革**：本条由用户 2026-10-07 指令设立；与既有条款「禁用而非删除时用最可逆的形式（字面注释 + file:line 恢复清单）」同源并推广到**一切实质性代码修改**。

## 代码约定

- C++23、`-fno-rtti`、静态 libc++；`-Wall -Wextra -Wconversion -Wsign-conversion`，
  新代码不得引入告警。include 用相对 `src/core` 的路径（如
  `#include "backend/cve_2026_43499/route/tcp_zerocopy_route.h"`）。
- 命名空间分层（现状）：顶层
  `ghostlock::{contract,pipeline,backend,platform,plugin,terminal,memory,session,race,kernelsnitch,support,profile,profile_entry,binary_profile,target,runtime_time}`
  （`config` 物理在 `session/`；顶层 `route`/`attack` 已并入 `backend::cve_2026_43499::route` / `memory`）；
  `contract` 持有中性词汇与契约，`pipeline` 只做组合/分派，backend 按 CVE 分子命名空间
  （`backend::cve_2026_43499::{route,backend_profile}`、`backend::cve_2026_43284::{ipsec,pagecache,lkm,steps}`）。
  `kernelsnitch/` 上游已冻结、可改写（归属 backend `leak` 模块），但须在顶级重构拆分后进行
  并保测试，不得顺手改。
- 全局状态只允许 `g_exploit_session`（进程唯一 singleton）与启动期只读的
  `g_direct_map_end`；新代码不得再引入可变全局或引用别名。审计记录见
  `docs/analysis/native-global-state.md`（git 历史）。
- 组件结构 = identity（kind，声明型，必须 host 可编译）+ 执行 policy（backend 步骤 / terminal 接管 /
  backend 内 route）。route policy 以编译期能力 + 静态 hook 表达，backend 步骤模板化在 route 上直接调用；
  Route 类满足 `prepare → execute → disarm → destroy`，仅经 `status` 汇报。新增组件
  （route/backend/terminal/platform/plugin）的完整触点清单见 `docs/development/adding-a-component.md`。
- 分层依赖由 **R1 include 防火墙**（`tests/include_firewall_test.cpp`）在 host 测试里强制：8 个受限源层，
  当前白名单 **0 条**（**空账本 + pin**：`kWhitelist` 必须为空；**R1 搬迁已完成**——`support/util.cpp` **799 → 133 行**（只留 **12 个中性函数**），14 个原 `support` 函数迁入新文件 `backend/cve_2026_43499/spray.cpp`（728 行，含匿名 namespace 门面 + 3 个文件内 static）与声明面 `spray.hpp`（51 行），9 处调用点全部改到 `cve_2026_43499::spray::*`；`kernelsnitch.h` 的**唯一 TU** 现为 `spray.cpp`（链 `spray.cpp → leak/address_discovery.h → kernelsnitch.h`）；`decls.hpp` 删 14 条声明并顺带删掉已无引用的 `memory/payload_builder.h`），运行输出
  `176 files, 0 forbidden-layer edges, 0 whitelisted, 0 unexpected, 0 stale`（γ 批后 `ancillary` 层更名 `plugin`：`plugin -> contract/memory/support` 允许；
  历史：173 → 174 = `root_child.hpp` 随 ADR-0006 F5 移入受限的 `backend/`；174 → 172 = R8 合并两份 SHA-256 为 `support/sha256.*`（删 4 增 2）；
  172 → 174 = P1 探针新增 `plugin/probe.{hpp,cpp}`；174 → 177 = P1 第二步新增 `plugin/{schema.hpp,wire.hpp,wire.cpp}`；
  177 → 179 = step 2 新增 `plugin/host.{hpp,cpp}`（插件宿主；step 3a 起**已接线**，但接线点在组合根与 backend 窗口，不在受限层）；
  179 → 180 = 43284 日志批新增 `backend/cve_2026_43284/diag_line.hpp`（有界结构化日志行）；**180 → 174 = 实测基线修正**（旧记录 180 偏高，与 UML §3.1 的「182 → 174」一致）；
  **174 → 176 = R1 搬迁**新增 `backend/cve_2026_43499/spray.{hpp,cpp}` 2 文件）。**历次变更均无新增越层边**，且本次搬迁后**账本归零**），
  不得 include `backend,pipeline,platform,terminal`）。新增的越层 include 会 FAIL；
  白名单条目对应的 include 消失（stale）同样 FAIL。新增组件优先不引入越层边，确需临时豁免时必须在
  `kWhitelist` 登记并写明 owner 批次，不得静默通过。
- 双侧一致性（改了必须两边同步，测试会抓）：
  - Native `contract/model.hpp` 的 `RouteKind`/`kRouteCatalog` ↔ Kotlin `data/route/RouteKind.kt`
    （`route_catalog_test.cpp` / `RouteCatalogAgreementTest.kt` 断言同一列表）
  - GLKv3 path→type FieldSpec ↔ Kotlin `NativeProfileGlkv3Adapter`，导出
    `app/src/test/resources/profile-manifest-v3.tsv`（`profile_manifest_v3_test.cpp` /
    `ProfileManifestV3AgreementTest.kt`）
  - **组合 token 白名单** ↔ Kotlin `CombinationCatalog`，导出
    `combination-manifest.tsv`（两份：`app/src/test/resources/` 对拍 + `profile-core/src/main/resources/` 运行时；
    `combination_manifest_test.cpp` 裸跑断言两份逐字节等于 `kCombinationCatalog`；
    `CombinationTokenAgreementTest.kt` / `CombinationTokenHardcodeTest.kt`）。生成命令：`make -C src combination-manifest`。
  - **组件词汇 manifest** ↔ Kotlin `VocabularyCatalog`，导出 `vocabulary-manifest.tsv`
    （4 类 kind = `backend` / `frontend` / `stepset` / `route`，**14 行**，两份逐字节一致：
    `app/src/test/resources/` 对拍 + `profile-core/src/main/resources/` 运行时；权威是
    `contract/identity.hpp` 的 kind 声明与 `kRouteCatalog`；生成命令 `make -C src vocabulary-manifest`；
    `vocabulary_manifest_test.cpp` 裸跑断言，Kotlin 侧 `ComponentKindTest` / `VocabularyManifestAgreementTest` 对拍）。
  - **stepset → 步骤序列 manifest**（M2/M3）↔ M3 资产迁移，导出 `stepset-steps.tsv`
    （`stepset<TAB>step_index<TAB>step_name`，**index 0 = 首个执行步骤**，**6 行** = 3 个 stepset；
    权威 = `contract/step_catalog.hpp` 的 `kStepSetAliases`（每条别名带**有序** `StepId` span）
    + `backend/cve_2026_43499/steps.hpp` 的 `W1W3Steps::kSteps`/`W1W2Steps::kSteps`（执行顺序，
    与别名表由 `static_assert` **逐项绑定**：任一侧调序即编译不过）；两份逐字节一致：
    `app/src/test/resources/` 对拍 + `profile-core/src/main/resources/` 运行时；生成命令
    `make -C src stepset-steps-manifest`；`stepset_steps_manifest_test.cpp` 在 `native-host-tests` 裸跑：
    **导出文本 == 冻结 pin** + 两份副本逐字节一致）。
  - **v2 owner Schema 的 manifest 已随 v2 一并删除**（S4-R2c）：`profile-manifest.tsv`、`profile_manifest_test.cpp`、
    `ProfileManifestAgreementTest.kt` 都不再存在；owner Schema（`platform/abi.hpp`、`backend/cve_2026_43499/*`）
    仍由 GLKv3 的 `schema == 3` 绑定路径使用，其声明权威是 `profile-manifest-v3.tsv`。
  - route 私有参数放 route 扩展节；只有共享代码会读的才进公共槽（顺序也必须一致）
  - **载体 vs canonical 双路径必须逐值同构（M5 教训）**：同一份选择数据经**两条读取路径**（「运行时载体」与「`available.<id>` 声明」）时必须**逐值等价**，且**声明路径只能作回退**——两条路径行为分叉＝**高危形态**（M5 前 `NativeProfileDocument.from()` 只读声明路径 ⇒ 导出的 `.bin` **静默丢队列** ✗；M5 改为「**载体优先、回退声明**」✓）。⇒ 新增任何「同一数据的第二条读取路径」必须同批补**跨路径等价对拍**。
- **构建逻辑一律写进 Gradle KTS（跨平台），禁止独立 `.sh` 构建脚本**（用户指令 2026-10-05）：LKM/DDK、插件产物、native 准备等一律由 `*.gradle.kts` 任务承担；**`.sh` 仅允许用于设备端与运维**（如 `tools/lkm/ghostlock/root_cmd.sh` 是设备载荷、`tools/device-guard/*`、`.github/scripts/*`）。跨平台硬要求：**不得**依赖 `shasum`/`sha256sum`/`mkdir -p`/`mv`/`cp`/`find`/bash —— 一律用 JVM/Gradle API（`MessageDigest`、`Copy`/`Sync`、`FileTree`）；工具链路径（如 `llvm-objcopy`）由 AGP 的 `android.ndkDirectory` 解析，**不依赖 PATH**；容器引擎探测 `podman`→`docker` 并允许 `-PcontainerEngine=` 覆盖。**跨端列表不得手抄**（如 8 个 KMI label）：由 native 导出 manifest（`lkm-kmi-manifest.tsv`）供 Gradle 与 Kotlin 消费。 **LKM/DDK 现状（2026-10-06，`8187375c`）**：LKM 由 **`buildLkmImages`**（逐 label 容器构建）/ **`copyLkmIntoAssets`**（fail-closed 落 assets）承担；**8 个 label 来自 native 导出的 `lkm-kmi-manifest.tsv`**（不手抄）；产物在**【仓库内】Gradle build 目录下**（`build/app/lkm/<label>/ghostlock.ko`；**源码目录 `tools/lkm/ghostlock/` 始终保持干净**，容器只在 build 目录里 `make`），账本 **`build/app/lkm/kmis.tsv`**（9 行 = 表头 + 8；源码树里没有该文件），守卫 **`:app:verifyLkmLedger`**（账本 × 缓存比对，**从全量 manifest × 缓存派生**）；APK 资产 = **8 个 `assets/lkm/<label>/ghostlock.ko`**（生成资产在 `build/app/generated/lkmAssets`）。**沿革**：历史上曾把 build 外移/软链到 `~/.ghostlock/build/root`（iCloud 规避）；**2026-10-06 用户规则改为「所有构建必须在仓库内真实 `build/`、禁止任何链接」，外移做法已废**。
- **构建产物必须在仓库内真实 `build/`，禁止任何链接（用户指令 2026-10-06）**：① 所有构建输出一律落 `<repo>/build/**`；② **禁止** `build` → 其他目录的**符号链接 / 硬链接 / junction**（历史做法 `build.nosync` 已废）；③ 出问题直接 `./gradlew clean` —— **产物可丢弃**，不得为保住产物而维护链接或副本；④ 若产物疑似来自旧树（历史上存在过 `build.nosync`）⇒ 用 `realpath` 核实并清理，**不要并行保留两棵树**。**依据**：本轮真实事故 —— 两套构建树并存 ⇒ 导出侧 `absolutePath` 与 `canonicalPath` 分叉 ⇒ 门禁任务图上游失败（另有 0 字节截断事故与原子写入要求）。
- 配置权威是 GLK profile（当前 wire 为 GLKv3）+ HOCON。执行层不得读配置类环境变量，只允许进程/路径类
  （`GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`）。需要新状态就扩展 profile。
- **版本号统一为 3（不要新增/叠加版本号）**：HOCON 配置与 wire 共用同一个数字，避免混淆。
  - **HOCON**：`schema_version = 3`（`app/src/main/assets/profile/*.conf`；被 `include` 的片段不带该键）。
  - **只有一个迁移点**：**Kotlin `LegacyProfileConverter.kt`**。App **写出恒为 3**；读到旧 `schema_version = 1` 时由它**转换为 3**（并记诊断），
    其余版本值一律拒绝（错误信息带实际版本）；
  - **wire（GLKv3）**：**MessagePack 文档**（根 map，必填 `schema == 3`，**无 magic/独立头**），解析用成熟单文件库 **MPack**（`src/lib/mpack`），
    canonical = 最短整数 + 键按 UTF-8 字节序排序；**native 只认 `schema == 3`**，
    **v2（对象分段 `header + sections[name → fields[name → u64]]`）已弃用**：旧 bin 一律拒绝，读路径与 v2 writer 一并删除；
  - **extractor**（`tools/extract_rs`）**只认/只产出最新版**：产出的 profile 为 `schema_version = 3`；
  - **App 版本**：`app/build.gradle.kts` 的 `appVersionName`（当前 `1.3`）；`versionCode` 由 `git rev-list --count HEAD` 派生。
  **静态策略进文档，运行时密钥/SPI/端口绝不进文档**（走会话帧，用后清零）。
  格式权威见 `docs/analysis/wire-transport-model.md`。Kotlin 与 native 版本绑定，同一分支内直接替换，
  不做长期并存；再需要不同格式时先改本节与 `docs/development/`，不要就地再起 v4/v5。
- v1（旧 `offsets.json`）**只在 Kotlin 侧**由 `LegacyProfileConverter.kt` 转换；
  native **不再解析 v1**（`src/core/legacy/` 的 JSON 路径已删除）。新 route/新字段不要改 v1 转换。

## 核心攻击代码审查（仅触及时执行）

以下审查只在改动触及核心攻击代码或其资源准备/回收路径时执行；普通 UI、文档、配置编辑器等未触及这些代码的改动不要求进行这组耗时检查。核心攻击代码包括 waiter/race/payload/route/exec 阶段代码，以及直接准备、持有、访问或回收其资源的代码。

- **所有权与生命周期追踪**：审查中按 Rust 式所有权思路列出每个关键对象、指针和资源的创建/获取起点、所有者、借用/访问者、访问区间、终结点及释放者。确认所有可能访问它的使用者都先于终结点停止访问；区分 owning、borrowed、shared 和 transferred ownership，并核对转移后旧所有者不再释放或访问。
- **UAF 检查**：逐条检查普通对象与资源路径，确认不存在 use-after-free；只有作为漏洞原语而被有意利用的目标对象生命周期例外，不得把该例外扩展到辅助对象、race 状态、waiter、缓冲区、映射或同步资源。
- **终结点与清理顺序**：确认销毁/回收发生在合理的生命周期边界，覆盖成功、失败、重试、取消和提前返回路径。重点核对 PI 相关内存与 waiter 生命周期：参与者停止访问、同步/解除关联、资源回收之间的既有先后关系不得因重构而改变；缺少明确终结点或顺序依据时不得合入。历史上曾因重构漏回收 PI 内存导致 panic，相关变更须特别检查资源回收位置和退出路径。
- **核心代码标记与资源生命周期核对**：在审查记录中标出核心攻击函数，以及它们对应的资源准备、存活期和回收代码，并按上面的所有权/UAF/终结点检查逐条核对。`tools/cmp_disasm.py` 可作为定位机器码改动范围的**辅助诊断（可选）**，但**一致性不是门槛**——判据是真机门禁。
- **范围与证据**：上述追踪表、差异结论和基线标识记录在该批次计划或审查记录中。检查仅针对被触及的核心代码及其资源生命周期闭包，不要求每个无关改动重跑；但该范围属于攻击关键路径时，仍须满足本文件“验证门槛”中的真机门禁及归档要求。

## 子智能体委派与停止规则

- **凡是可并行、可自包含的工作，优先委派给子智能体**（不限于门禁）；**若被委派任务本身还能再并行拆分，子智能体应继续递归创建自己的子智能体**，并把写范围继续切分到互不重叠。
- **子智能体要能复用（强制）**：优先用 `spawn_teammate` 建**常驻 teammate**（稳定 target、可 `send_message` 追加任务、可 `interrupt`），
  而非每批新起一次性 agent；**同一模块/写范围固定由同一个 teammate 负责**，其历史上下文因此可复用（不必每次重新通读仓库）。
  常驻流建议划分：`native-core`（src/core）、`kotlin-app`（profile-core + app + assets）、`docs-uml`（docs/UML/计划/门禁归档）。
  同一时刻只允许**一条写入流**；未获指派的 teammate 只做只读勘察。
  - **门禁**（host / NDK / lint / cmp_disasm / 真机）——耗时长，委派后主智能体继续推进；
  - **独立实现/调研**：写范围与主线不重叠的文件改动、上游事实核查、测试补写、文档起草、跨模块对拍。
- 委派必须给出：自包含的目标、涉及的精确文件/写范围、命令与期望结果、验收标准；子智能体只做被委派的事。
- **写范围不重叠**：子智能体与主智能体共享工作树，同一文件不得并行修改；门禁运行期间不得改被该门禁覆盖的源文件。
- **共享缓存的构建必须串行（本项因一次真实撞车而设立）**：跨 writer 共享的【**容器引擎镜像/层缓存**】与 `<repo>/build/**` 下的**共享产物**，同一时刻**只允许一个 writer 跑构建** —— 历史上曾用 `~/.ghostlock/lkm/**` 作共享目录（**现已不用**，产物一律落仓库内 `build/`）；那轮真实撞车为两个 writer 同跑 `buildLkmImages` ⇒ `work/.tmp_*` 与 `Makefile` 缺失（**锚点未被破坏**）。⇒ 需要并行时用 **`-PlkmLabel=` 单标签 + 独立 workdir**，或**串行排队**；构建期间不得有第二个 writer 触碰同一缓存。
- **Gradle 门禁必须串行（本项因一次真实并发而设立）**：`:app:testDebugUnitTest` / `buildLkmImages` / `verifyLkmLedger` 等**写同一 build 目录与共享缓存**的任务，**同一时刻只跑一个**；并发跑会互相污染（临时候选目录被删、XML 结果被覆盖）。⇒ 门禁排队执行，或在隔离 workdir 里跑。
- **编辑锚点必须是语义边界（禁止「文本片段」/「下一个括号」式切法）**：删改代码块时锚点必须**整语句/整块**（含结尾分号与注释），**不得**用「从某关键词到下一个 `}`」这类相对切法——本项目曾因此**截断多行语句**、留下悬空常量或未闭合注释（删 defex 时弄坏 3 个文件）。⇒ 批量编辑＝**先校验全部锚点、任一不匹配则整体不写**（事务式），改后**编译 + 门禁**验证。
- **程序里不要用反引号（本项因一次真实截断而设立）**：用脚本/程序生成或替换文本时，**载荷里不得含反引号字符**（shell 命令替换会截断/执行它）；需要字面反引号时用 `\x60` 或写入临时文件。**脚本运行前必须自检「载荷不含反引号」**（不通过就整体不写）。
- **同一批次门禁连续失败 2 次即停止前进**，回到 debug（复现、定位、修复）；门禁未绿不得进入下一批。
- **提交纪律（Lead，强制；本项因两次真实事故而设立）**：提交前必须**同时**满足 ① **门禁在最终树上为绿**（脚本必须**检查退出码**，不得无条件继续到 `git commit`）、② **没有任何 writer 正在改被该门禁覆盖的文件**（有人在中途改代码时跑门禁会得到**假红**，此时**不得**据此提交或回退，应等其停手或**在隔离 worktree 上按提交验证**）。
- **假红/假绿的处理**：门禁结果与「当前是否有 writer」冲突时，正确做法是 `git worktree add <tmp> <commit>` **在隔离检出上按该提交重跑门禁**——这既能验证提交、也不受在途改动干扰。
- **「是否有 writer」的判据是成员状态，不是文件 mtime**（本项因两次误判而补充）：`list_agents` 中**没有 running/provisioning 且写范围覆盖该文件的成员**时即可跑门禁与提交；**刚停手的成员会留下极新的 mtime**，按 mtime 判定会造成**假跳过**。
- **死代码判定（本项因一次判断失误而设立）**：**跨进程 / 第三方契约的成员，「本仓无调用者」不足以判定死代码**——必须先查外部调用方（例：Shizuku 管理器会调用 UserService 的 `destroy()`；删掉会破坏服务生命周期契约）。只有「仓内与外部均无调用方」才可删。
- **禁用而非删除时用最可逆的形式**：用户要求「暂停某功能」时，**先按其字面要求执行**（本项目用户明确要求字面注释而非开关）；恢复清单必须**逐条到 file:line**，使解冻＝撤销注释 + 跑门禁，而非考古。
- **用户提出与既有设计/决定冲突的想法时，Lead 必须当场质疑并明确列出冲突（本项由用户明确要求而设立）**：不得因为「是用户提的」就沉默执行，也不得**悄悄**改掉既有设计。正确做法：① **先指出冲突点**（引用既有决定/代码/物证，说明为什么冲突）；② 给出**选项与代价**（含「照新想法做」这一项及其影响面）；③ 由用户裁决；④ 用户裁决后**照做**，并在计划/契约里**记录沿革**（旧决定为何被取代，不删历史）。**质疑 ≠ 拒绝**——把冲突和代价讲清是职责，最终决定权在用户。
- **脚本化编辑必须语句级、锚点级**（本项因一次真实事故而设立）：用「行包含关键字就删」这类粗暴规则会**截断多行语句**、留下悬空常量或未闭合注释（本项目一次删 defex 时就弄坏了 3 个文件）；批量编辑必须**先校验全部锚点、任一不匹配则整体不写**（事务式），并在改后**编译 + 门禁**验证。
- 真机门禁仍需按 `docs/analysis/device-gates/` 归档；子智能体返回原始日志与退出码，主智能体落地归档与记录。

- **禁止任何改变共享工作树的 git 命令与等效写回（本项因一次真实事故而设立）**：任何 teammate **不得**执行 `git checkout` / `git restore` / `git stash` / `git reset` / `git clean` 等会改变工作树内容的命令，**也不得**用等效写回（例如 `git show REV:path > path` 覆盖文件）。历史比较**只用只读形式**：`git show <rev>:<path>`（不重定向到工作树）、`git diff <rev> -- <path>`、`git log`。**依据**：本项目一次真实事故 —— 该写回把工作树未提交的今日改动整体覆盖为 HEAD 版。
- **禁止分片读 + 全文写回（本项因同一次真实事故而设立）**：`read` 带 `offset/limit` 只返回窗口，而 `write` 会**整体替换**文件 ⇒ **禁止**把窗口内容写回。读文件必须**整份读**（不带 `offset/limit` 或确认读到 `totalLines`），或改用**定点 `edit`**；若确需整份写回，**写回前后必须核对字节数与行数**并在报告中给出（本次事故 = 5 份文件被截断，其中 1 份不可恢复）。

## 设计与规划审查岗 design-critic（2026-10-07 用户裁定 · 强制）

- **岗位**：常驻 teammate `design-critic`（**只读**：不写文件、不跑门禁、不碰代码）；**同一审查岗固定同一成员**，不得每批新起（复用上下文与原则版本）。
- **审查对象**：L 级设计（`docs/analysis/**` 与 `docs/plan/**` 的设计文档）、ADR、批次计划、wire/profile 契约改动，以及任何「新增组件/功能/结构」的提案。
- **依据**：`docs/development/design-review-principles.md`（当前 **v0.2**）；权威顺序 **守则 > AGENTS > engineering-standards/design-philosophy > analysis 文档**。
- **强制规则**：
  1. **非作者评审**（作者不得自审）；
  2. 必须基于**冻结快照**，报告记录 **HEAD/文档哈希/时间**；期间有 writer 改动 ⇒ 声明「**不覆盖**」并在冻结后复跑；
  3. **输出格式固定**：不符合项表 = `编号 | 严重度 | 位置 | 问题 | 必须怎么改 | 依据`；末尾一行总判「**可进批准 gate / 仍需修订（列必改）**」，并**单列**「验证了什么 / 没验证什么」；
  4. 每条**依据可核验**（`file:line` 或原则编号 / R 编号）；**禁止**引用外部计划或全族**未定义标签**；
  5. **严重度口径**：阻断 / 重要 / 建议；**阻断项未关闭不得进批准 gate**；
  6. **设计者必须逐条回应**（接受并改 / 举证反驳），不得沉默忽略；
  7. 用户**强制确认**的越权，审查者必须核对**沿革记录与技术债登记**是否都存在；
  8. **触发时机**：L 级设计定稿前必审 · 批次计划变更必审 · 原则版本更新后重跑受影响范围；
  9. **记录义务**：每次审查留一行可检索记录（时间 · 原则版本 · 总判 · 阻断数）。
- **与其它评审的分工**：原则/简并面 = **design-critic**；native 技术面 = native-core；Kotlin/配置面 = kotlin-app；文档/配额/同批面 = docs-uml。四方结论交 Lead 合并，冲突时以**原则与物证**裁决。
- **当责**：不得因提出者身份（用户/Lead/队友）而放行；质疑必须给出**代价与合规替代**；不得阳奉阴违。

## 验证门槛

- **守卫/断言必须证明「能失败」**（本项因一次真实发现而设立）：新增的守卫、断言、检查**不能只证明「现在通过」**——必须做一次**证伪实验**（临时造错 → 观察它以预期方式失败 → 撤回并核验无残留），报告里写明造错点与失败输出。本项目曾因此发现一条 **vacuous 断言**：`memcmp` 比较两个 value-init 结构时**恒非零**（padding 未归零）⇒ 删掉被保护的赋值它**仍然绿**；修法＝两侧先 `memset` 归零 + `static_assert(is_trivially_copyable)`。
> 2026-10-07 用户裁定：本条由审查原则 **D1** 取代（黑盒/白盒 + 极端输入值；**不可能用例不测**）——见 `docs/development/design-review-principles.md`；**本条保留为可选手段（不再强制）**。
- **证伪/验证实验一律用 `make -B`**（本项因一次假证明而设立）：`make` 会因**同一秒 mtime** 判定 up-to-date 而**跑旧二进制**，给出**假证明**；凡「造错后验证会失败」的实验必须强制重建。
- **证伪实验必须清理被测二进制（本项因一次真实假红而设立）**：造错实验结束后，**不仅要还原源码，还必须删除/重建被测二进制**（`rm -f <artifact>` + `make -B` 复核）—— 源码回位 ≠ 产物回位；本项目曾因只还原源码、留下**探针二进制**，使随后跑门禁的人**跑到探针**并获得**假红**（`queue_wire_test.cpp:439` 既有断言被误报 ✗）。交回前一律 `rm -f <artifact>` 并 `make -B` 复核。
- **判断产出必须数产物、不数目录（本项因一次误判而设立）**：`ls | wc -l` 会把**空目录/临时目录**算进去 ⇒ 必须数**产物文件**——LKM 例：`buildLkmImages` 的 **8 行 `LKM <label> -> … (bytes)`**、`kmis.tsv` **9 行**（表头 + 8）、`unzip -l … | grep assets/lkm/` **8 行**；判定「生成了几个」时一律用这些计数。
- **失败数必须读 XML 全量（本项因一次真实漏数而设立）**：单测失败**不得**只看控制台尾部（会被截断/漏掉），必须**全量读** `build/test-results/**/*.xml` 统计 `tests/failures/errors/skipped`；**三桶计数（通过 / 失败 / 跳过）一律来自 XML**，控制台只作旁证。
- 普通改动：`make -C src native-host-tests` + NDK 构建零警告 + `make -C src lint-tidy`。
- 攻击关键路径（waiter/race/payload/route/exec 流程）改动：
  1. **真机门禁**（唯一权威判据：冷机、固定 CPU 对、单 route、KernelSU 未加载的干净启动）；
  2. 日志在设备 `Download/ghostlock-debug-log/<时间>/*.log.txt`（同目录另有
     `profile.conf`/`profile.bin`，记录本次生效配置与送入 native 的 GLKv3 字节），确认 route 命中与写验证通过；
  3. 结果按 `docs/analysis/device-gates/*.md` 归档。
  可选：`tools/cmp_disasm.py`（基线 `build/native/ghostlock-B0`）用于定位机器码改动范围，**不作门槛**。
- 真机结果按 `docs/analysis/device-gates/*.md` 的格式归档（git 历史中有整套
  S/CPP/U01/NS\* 证据链样例）。
- `KERNEL-PANIC-01` 是已知环境/时序问题：同构建可 PASS/panic/PASS，判定因果要求同构建
  复现 + 冷机复跑，不要仅凭一次 panic 归因代码。
- 现实状态：Multicast（5.15）、TCP、Select 三条路径均已由开发者真机验证；新 profile
  未过真机不得标 supported。

## 文档约定

- 完整规范见 `docs/development/documentation-standards.md`（分类地图、命名与结构、中英写作、
  RFC 2119 用语、画图要求、计划/门禁模板、归档流程、检查清单；依据 ISO/IEC/IEEE 42010/15289/2651x、
  Diátaxis、DITA、Minimalism、Google style）。
- **改动必须更新 UML（强制）**：全流程权威图 = `docs/development/full-process-uml.md`
  （IPO / 状态机 / Class / Sequence 四类；Class 按 C++ `namespace`、Kotlin `package`、Rust `module` 分组）。
  任何**结构性改动**——新增/删除 backend、terminal、插件、**组合 token**、状态机状态、wire 字段，
  或类/函数的职责与归属变化——必须**在同批**更新该文件，并在提交信息里写明「更新了哪一张图」；
  评审与门禁按此检查。一个结构只保留一处权威图，其他文档链接它，不重复画同一结构。
- 双语：`README.md` + `README_ZH.md`；`docs/**` 下有 `*_ZH.md` 对应的保持同步。
- 现行文档：`README.md`、`docs/profile/*`、`docs/development/adding-a-component.md`、
  **`docs/development/full-process-uml.md`（全流程 UML 权威：IPO/状态机/Class/Sequence，改动必更新）**、
  `docs/development/design-philosophy.md`（设计思想，改动前必读）、
  `docs/development/engineering-standards.md`（工程规范，做法与门槛）、
  `docs/development/documentation-standards.md`（文档规范）、`src/core/README.md`。
- 现行主计划：`docs/plan/MASTER-PLAN.md`（唯一进度入口）；历史计划已归档 `docs/archive/20261007-2237-*`；决策/门禁见
  `docs/analysis/adr/`、`docs/analysis/device-gates/`。
- **历史文档：已恢复到 `docs/archive/`**（2026-10-06 用户指令「把以前提交又删掉的文档都找回」）：命名 = **`YYYYMMDD-HHMM-<原文件名>`**（时间 = **删除提交的时间**），每件文件头含**原始路径 / 删除提交 / 恢复来源（`git show <commit>^:<path>`）/ 恢复日期**；「删除提交 → 归档路径」完整索引见 **`docs/archive/INDEX-device-gates.md`**（README §五 为指针）。
  - **证据类已恢复**：`docs/analysis/device-gates/**` 历史门禁 ⇒ **已恢复到 `docs/archive/device-gates/`（164 件，实测 `find docs/archive/device-gates -type f | wc -l` = 164）**，命名与头部规则同文档类（见 `docs/archive/INDEX-device-gates.md`）；**未恢复**：`docs/kernel_profiles/templates/**` **4 件**（已被现役 `docs/profile/templates/**` 取代，理由见 README §六）。
  - 兜底：仍可从 git 历史取回 `git show <commit>:<path>`。以下保留原删除清单分类作对照：
  - `docs/analysis/` 其余架构/迁移/解耦分析（routes、native-functions、native-cpp-current-uml、
    native-global-state、native-entrypoint-plan、environment-convergence-plan 等）
  - `docs/analysis/device-gates/`：S04–S15、CPP00–CPP17、U01、NSFUNC/NSMOD/NSMOD2、
    PROFILE-\* 门禁证据链
  - 根目录：`ARCHITECTURE.md`、`DECOUPLING_PLAN.md`、`DECOUPLING_LOG.md`、
    `PR_DESCRIPTION.md`、`RELEASE_NOTE.md`；`docs/pr-note-*`、`docs/release-note-v1.*`、
    `docs/development/native-modernization-plan.md`
  - `repro/xperia-first-success/`（Xperia 5.15 首攻 V1–V21 实验史）

## 排障

- Gradle native 构建找不到 NDK：设 `ANDROID_NDK_HOME` / `ANDROID_NDK_ROOT` 或
  `local.properties` 的 `ndk.dir`。
- Rust 交叉编译 Android：必须用 rustup 工具链的 `rustc`（Homebrew rustc 缺
  `aarch64-linux-android` std），并 `rustup target add aarch64-linux-android`。
- 生成物统一在 `build/` 下；不要提交 `build/`、`*.so`、`local.properties`。
- 提交流程：分支 `very-not-stable-dev` → 上游 `main`；提交信息用
  `type(scope): summary`（feat/fix/docs/refactor/test/chore）。未经要求不要 commit/push。
