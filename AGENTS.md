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
  CLI：`--ghostlock-app-call`（stdin 读长度前缀的 GLKv3 文档，其后可接运行时密钥会话帧）/
  `--load-prebuilt-profile <bin>`；另有仅用于 43284 开发/门禁的
  `--run-cve-2026-43284 <ko> <target> --stage=plan|write|trigger|full`（生产 fail-closed）。
  无参数的 v1 `offsets.json` 入口已移除；入口细节见
  `docs/analysis/native-entrypoint-plan.md`（git 历史）。
- 内置 profile 在 `app/src/main/assets/kernel_profiles/`：`index.conf` 索引、
  `<uname-r>.conf` 每 release 一份、`execution-*.conf` 公共/分 route 调参、
  `credential-6x.conf`、`kernelsnitch-6x.conf`。格式为 HOCON（支持 `include`）。
- 组件模型（ADR-0004）：装配轴为 **backend × terminal**，由 `Pipeline<Backend, Terminal>` 编译期固定；
  backend 提供漏洞原语与写入步骤，terminal 承载启动/交接。当前 catalog 收录 **3 个稀疏 triple**：
  `cve_2026_43499 × {w1_w3, w1_w2} × root_child`（可用）与
  `cve_2026_43284 × pagecache_write × umh_forward`（已接线、未可用，等 B5-9 真机门禁）。
  每轴可用性：backend 仅 `cve_2026_43499`；`cve_2026_64560`/`cve_2026_31431`/`cve_2026_43503`/`cve_2026_23274` 为纯头占位不可用；
  terminal 仅 `root_child`（可用）与 `umh_forward`（执行 policy 已落、未可用）；旧文档的 `file_write`/`panic` 占位**不存在**。
  route（`select_stack`/`tcp_zerocopy`/`multicast_waiter`）是 backend 内部策略，按 profile 选，不是装配轴；`platform`/`ancillary` 为横切。
- 契约/组合分工：`contract/identity.hpp` 定义 kind、每轴可用性谓词、identity 声明与执行概念（host 可编译）；
  `pipeline/component_catalog.hpp` 是**组合（wiring）的唯一权威**——稀疏 triple、`DispatchTarget`、`combination_supported`/`dispatch_target_of`，
  词汇来自 `contract`；`pipeline/orchestrator.hpp` 按 catalog 分派，每个 case 用 `Pipeline::target` static_assert 锁定。
  `selection_supported()`（设备已核实）与 `combination_supported()`（已接线）是两个不同问题；选择显式来自 profile/wire，不从 kernel 版本推断。

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
./gradlew exportKernelProfiles        # 生成 GLKv3 .bin 到 build/kernel-profiles/
(cd tools/extract_rs && cargo test --release)

# 攻击函数形状对比（攻击路径改动必须跑；已确认基线 build/native/ghostlock-B0）
python3 tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock
```

## 改动流程：先设计，后改动（强制）

先读 `docs/development/design-philosophy.md`（设计思想：11 条原则，以 SWEBOK/ISO 12207/ISO 25010
等软件工程理论为依据，含理论锚点与项目实例）。
完整工程规范见 `docs/development/engineering-standards.md`（架构/数据流/控制流/数据结构/代码风格/
文档/验证的详细规则 + 外部标准来源）。**任何改动前必须先完成设计**：

1. **Explore**：先读代码与文档；用 `git log --all -- <path>` 查历史设计与 device-gate 证据
   （现行计划见 `docs/analysis/branch-plan.md`，其余分册在 `docs/analysis/` 或 git 历史里）。
2. **Design**：S 级（注释/格式）直接改；M 级写清动机/影响文件/行为差异/验证计划；**L 级**
   （攻击关键路径、wire/profile 格式、跨 Native↔Kotlin 契约、公共数据结构、新增 route）
   必须先产出计划文档（模板见 `docs/development/documentation-standards.md`）并获用户认可，再写代码。
3. **Implement**：最小改动，只碰设计列出的文件；不动 `LegacyProfileConverter.kt` 的 v1 转换与
   规范中的"明确保留"清单。`kernelsnitch/` 上游已冻结，可在顶级架构重构中作为 backend 模块
   拆分/改写，但必须先有设计与测试，不得顺手改。
4. **Verify**：按级别跑满 §1.3 门槛并保留证据；汇报时给出命令与结果，不写"应该没问题"。

- 攻击路径改动 = `cmp_disasm`（`TARGETS` 6 组攻击函数）+ 真机门禁 + 门禁记录（格式见
  `docs/development/documentation-standards.md`），缺一不可。
- 大改动按批次推进，一个批次只做一类事，上一批验证通过再进下一批。

## 代码约定

- C++23、`-fno-rtti`、静态 libc++；`-Wall -Wextra -Wconversion -Wsign-conversion`，
  新代码不得引入告警。include 用相对 `src/core` 的路径（如
  `#include "backend/cve_2026_43499/route/tcp_zerocopy_route.h"`）。
- 命名空间分层（现状）：顶层
  `ghostlock::{contract,pipeline,backend,platform,ancillary,terminal,memory,session,race,kernelsnitch,support,profile,profile_entry,binary_profile,target,runtime_time}`
  （`config` 物理在 `session/`；顶层 `route`/`attack` 已并入 `backend::cve_2026_43499::route` / `memory`）；
  `contract` 持有中性词汇与契约，`pipeline` 只做组合/分派，backend 按 CVE 分子命名空间
  （`backend::cve_2026_43499::{route,backend_profile}`、`backend::cve_2026_43284::{ipsec,pagecache,lkm,steps}`）。
  `kernelsnitch/` 上游已冻结、可改写（归属 backend `leak` 模块），但须在顶级重构拆分后进行
  并保测试，不得顺手改。
- 全局状态只允许 `g_exploit_session`（进程唯一 singleton）与启动期只读的
  `g_direct_map_end`；新代码不得再引入可变全局或引用别名。审计记录见
  `docs/analysis/native-global-state.md`（git 历史）。
- 组件结构 = identity（kind，声明型，必须 host 可编译）+ 执行 policy（backend 步骤 / terminal 接管 /
  backend 内 route）。route policy 以编译期能力 + 静态 hook 表达，backend 步骤模板化在 route 上直接调用
  （不用虚基类）；Route 类满足 `prepare → execute → disarm → destroy`，仅经 `status` 汇报。新增组件
  （route/backend/terminal/platform/ancillary）的完整触点清单见 `docs/development/adding-a-component.md`。
- 分层依赖由 **R1 include 防火墙**（`tests/include_firewall_test.cpp`）在 host 测试里强制：8 个受限源层，
  当前白名单 3 条（均为 `support/util.cpp` → 43499 backend，注明 owner 批次），运行输出
  `142 files, 3 forbidden-layer edges, 3 whitelisted, 0 unexpected, 0 stale`。新增的越层 include 会 FAIL；
  白名单条目对应的 include 消失（stale）同样 FAIL。新增组件优先不引入越层边，确需临时豁免时必须在
  `kWhitelist` 登记并写明 owner 批次，不得静默通过。
- 双侧一致性（改了必须两边同步，测试会抓）：
  - Native `contract/model.hpp` 的 `RouteKind`/`kRouteCatalog` ↔ Kotlin `data/route/RouteKind.kt`
    （`route_catalog_test.cpp` / `RouteCatalogAgreementTest.kt` 断言同一列表）
  - GLKv3 path→type FieldSpec ↔ Kotlin `NativeProfileGlkv3Adapter`，导出
    `app/src/test/resources/profile-manifest-v3.tsv`（`profile_manifest_v3_test.cpp` /
    `ProfileManifestV3AgreementTest.kt`）
  - v2 owner Schema（`platform/abi.hpp`、`backend/cve_2026_43499/*`）↔ Kotlin `NativeProfileDocument`，导出
    `app/src/test/resources/profile-manifest.tsv`（`profile_manifest_test.cpp` /
    `ProfileManifestAgreementTest.kt`）
  - route 私有参数放 route 扩展节；只有共享代码会读的才进公共槽（顺序也必须一致）
- 配置权威是 GLK profile（当前 wire 为 GLKv3）+ HOCON。执行层不得读配置类环境变量，只允许进程/路径类
  （`GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`）。需要新状态就扩展 profile。
- **不要随意新增或叠加配置/profile 传输格式版本号**。本分支的 wire 已是 **v3（GLKv3）**：
  **MessagePack 文档**（根 map，必填 `schema == 3`，**无 magic/独立头**），解析用成熟单文件库 **MPack**（`third_party/mpack`），
  canonical = 最短整数 + 键按 UTF-8 字节序排序；写出 v3，v2（对象分段
  `header + sections[name → fields[name → u64]]`，presence 由键是否出现表达）**只读**兼容。
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
- **核心代码标记与反汇编核对**：在审查记录中标出核心攻击函数，以及它们对应的资源准备、存活期和回收代码。每次触及核心攻击代码后的重构，都用 `tools/cmp_disasm.py` 对比已确认的基线二进制，并反汇编检查核心攻击代码和相关资源准备/回收代码的机器码及相对顺序；要求攻击代码与准备/回收之间的顺序保持不变。若字节差异无法证明不影响该不变量，停止该批次并调查，不以测试通过替代反汇编核对。
- **范围与证据**：上述追踪表、差异结论和基线标识记录在该批次计划或审查记录中。检查仅针对被触及的核心代码及其资源生命周期闭包，不要求每个无关改动重跑；但该范围属于攻击关键路径时，仍须满足本文件“验证门槛”中的完整 `cmp_disasm`、真机门禁及归档要求。

## 子智能体委派与停止规则

- **凡是可并行、可自包含的工作，优先委派给子智能体**（不限于门禁）；**若被委派任务本身还能再并行拆分，子智能体应继续递归创建自己的子智能体**，并把写范围继续切分到互不重叠。
  - **门禁**（host / NDK / lint / cmp_disasm / 真机）——耗时长，委派后主智能体继续推进；
  - **独立实现/调研**：写范围与主线不重叠的文件改动、上游事实核查、测试补写、文档起草、跨模块对拍。
- 委派必须给出：自包含的目标、涉及的精确文件/写范围、命令与期望结果、验收标准；子智能体只做被委派的事。
- **写范围不重叠**：子智能体与主智能体共享工作树，同一文件不得并行修改；门禁运行期间不得改被该门禁覆盖的源文件。
- **同一批次门禁连续失败 2 次即停止前进**，回到 debug（复现、定位、修复）；门禁未绿不得进入下一批。
- 真机门禁仍需按 `docs/analysis/device-gates/` 归档；子智能体返回原始日志与退出码，主智能体落地归档与记录。

## 验证门槛

- 普通改动：`make -C src native-host-tests` + NDK 构建零警告 + `make -C src lint-tidy`。
- 攻击关键路径（waiter/race/payload/route/exec 流程）改动：
  1. `tools/cmp_disasm.py` 对比 `TARGETS` 6 组攻击函数（基线 `build/native/ghostlock-B0`），
     要求 IDENTICAL (strict) 或已复核的注解差异；
  2. 真机门禁（冷机、固定 CPU 对、单 route、KernelSU 未加载的干净启动）；
  3. 日志在设备 `Download/ghostlock-debug-log/<时间>/*.log.txt`（同目录另有
     `profile.conf`/`profile.bin`，记录本次生效配置与送入 native 的 GLKv3 字节），确认 route 命中与写验证通过。
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
- **结构与流程变化必须同步更新对应 Mermaid/UML 图**：L 级改动（route、跨层契约、会话/资源所有权、
  攻击阶段机、清理边界）必须画图；一个结构只保留一处权威图，其他文档链接它。
- 双语：`README.md` + `README_ZH.md`；`docs/**` 下有 `*_ZH.md` 对应的保持同步。
- 现行文档：`README.md`、`docs/kernel_profiles/*`、`docs/development/adding-a-component.md`、
  `docs/development/design-philosophy.md`（设计思想，改动前必读）、
  `docs/development/engineering-standards.md`（工程规范，做法与门槛）、
  `docs/development/documentation-standards.md`（文档规范）、`src/core/README.md`。
- 现行分支计划：`docs/analysis/branch-plan.md`（唯一进度入口）；决策/门禁见
  `docs/analysis/adr/`、`docs/analysis/device-gates/`。
- 历史文档（已从工作树删除，需要时从 git 历史取回：`git show <commit>:<path>`）：
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
