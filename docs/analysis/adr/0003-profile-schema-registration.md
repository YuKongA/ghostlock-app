# ADR-0003：profile 注册模型（中性 document + owner schema）

- 状态：Proposed（待维护者确认）
- 日期：2026-10-03
- 基线：`acc5e7b` + 在飞改动（`B0`，见 Phase 0 计划）
- 相关：ADR-0001 §10/§12；`docs/analysis/top-level-architecture-rewrite-plan.md` 架构审查 A；
  `docs/kernel_profiles/PROFILE_SCHEMA.md`；`docs/development/engineering-standards.md`；
  `docs/analysis/flexible-kernel-rw-primitive-plan.md`

## 背景（Context）

- **设计目标**：profile 必须能完整表达任意 route/path 的任意参数。因此 profile **包含具体后端参数是必需**
  的，不是缺陷；问题只在于由谁声明、谁绑定。
- **现状**：`profile/binary.cpp` 用单体 `kSections` 直接绑定 `profile::kernel_offsets` 成员，把平台 ABI
  （`task_struct`/`cred`/`offset`）、设备 phys（`kernel_phys_*`）、厂商 vr（`vr_guard`）与 43499 的
  route/execution/泄漏/spray 字段混在**一张表 + 一个 struct**里。`binary_profile::parse` 还硬编码
  `kernel_offsets*` 与三条合法 route。
- **三端手写键名**：native `binary.cpp`、Kotlin `NativeProfile.kt`（`sections()`）/`ProfileModel.kt`、
  extractor `report.rs`/`symbols.rs`。加一个字段要改三处，漂移靠零散测试兜。
- **P6 / 架构审查 A**：平台与后端字段来自**同一份 GLK1 wire**；平台若要读内核 ABI，就得依赖 backend 私有
  的 `TargetProfile`（破坏 DAG）或重复解析 wire（SSOT 分裂）。

## 决策（Decision）

1. **wire 不变**。GLK1 v2 保持 `header + sections[name→fields[name→u64]] + presence`（见 `profile/binary.h`）。
   本 ADR 只改**内部绑定**，不新增/叠加格式版本号（遵守 AGENTS 的版本策略）。
2. **中性 `profile::Document`**。容器只做 framing：解码出只读 document（`release`、component ids、每个
   section 的 `key→{raw u64, 原始宽度, present}`）。Document **不命名任何字段、不解释 route**。
3. **owner 注册 schema**。每个 owner 声明：
   - `Schema`：编译期 `FieldSpec[]`（`section`、`key`、`kind`（宽度/符号）、`required`）；
   - `View`：该 owner 的 typed 结构；
   - `bind<Schema>(const Document&) -> View`：集中校验 presence/宽度/必填，**fail-closed**。
4. **注册机制（编译期）**。组合点维护 `SchemaList`（每 owner 一个 Schema；backend 可含每 route 子 schema），
   仿现有 `BackendIdentityList`；`bind_all<SchemaList>(document)` 模板遍历。**不引入运行期可变注册表**
   （否则回到“单可变全局”与静态初始化顺序问题）。document 只在启动期解码一次，View 在启动期绑定，热路径只读。
5. **所有权规则（interpreter ownership）**：字段归**解释它的层**；每个 `(section, key)` **恰一个 owner**；
   跨 owner 重复由编译期/测试查重拒绝。backend 需要平台字段时，**依赖并使用平台的 View**，不重复声明。
6. **`kernel_offsets` 降级为 43499 的 View**。`TargetProfile` 是其封装；它不再是共享权威结构，平台另有自己的
   View。P6 的“一处字段映射”落在各 owner 的 Schema 上。
7. **组件 id / route id 合法性由 owner 校验**：容器只搬运 header 里的 `frontend/backend/middleware` u16；
   注册表提供“已注册的 wire id / route id”与校验。这同时消解 catalog↔backend 的 include 环（架构审查 H）。
8. **未知字段策略（生产 strict）**：已知 owner section 内的未知 key、以及不属于任何注册 schema 的 section
   → **拒绝**；仅工具/编辑模式允许 ignore。现状是 ignore（`binary.cpp:337` `if (!section) continue;`），
   需改为 strict（fail-closed，且能把拼写错误挡在启动期）。
9. **三端一致性**：native Schema 为权威，产出机器可读 **manifest**（`section/key/宽度/owner`）；Kotlin 与
   extractor 的键名对 manifest 做测试（subset/equality）。P6“一处字段映射”由此机械化。

## 结构

```mermaid
flowchart LR
  W[GLK1 v2 bytes] --> C[profile 容器 framing]
  C --> D[profile::Document<br/>sections[key->raw u64 + presence]]
  D --> BA[bind platform::abi::Schema]
  D --> BV[bind platform::vivo::Schema]
  D --> BB[bind backend::cve_2026_43499::profile::Schema<br/>（含每 route 子 schema）]
  D --> BH[bind handoff::Schema]
  BA --> VA[platform::abi::View]
  BB --> VB[TargetProfile / kernel_offsets View]
  BV --> VV[platform::vivo::View]
  BH --> VH[handoff::Settings]
  M[native Schema -> manifest] -.-> K[Kotlin NativeProfileDocument]
  M -.-> X[extractor report.rs / symbols.rs]
```

## 字段划分（初始提案）

规则：内核结构布局与设备事实 → `platform`；厂商 → `platform::vivo`；漏洞机制/route/执行调参/泄漏/spray →
backend；跨 backend 的后处理调参 → `handoff`；通用运行调参 → session/pipeline。

| 现 section.key | 归属 | 备注 |
|---|---|---|
| `task_struct.*` | `platform::abi` | 内核 ABI 布局 |
| `cred.{usage_offset,caps_offset,ref_count,ref0..3_offset}` | `platform::abi` | struct cred 布局 |
| `offset.{init_task,init_cred,empty_zero_page,root_task_group,selinux_enforcing,selinux_blob_sizes,security_hook_heads}` | `platform::abi` | 内核符号 |
| `kernel.{kernel_phys_load,kernel_phys_offset}` | `platform::abi` | 设备 phys |
| `meta.vr_guard`、`offset.vr_sys_exit_tp`、`vr_guard.tracepoint_funcs` | `platform::vivo` | 厂商 vr |
| `cred.{copy_size,usage_value,caps_count,caps_value,ref0..3_image}` | backend | 提权目标策略值（布局另属 platform） |
| `offset.{slide_nfulnl_logger,slide_loggers_0_1,slide_boot_id}` | backend | KASLR slide 泄漏目标 |
| `kernel.{compact_waiter,kernelsnitch_collisions,mm_struct_sz}` | backend | route/spray/泄漏几何 |
| `meta.{fallback_route,safe_mode}` | backend | 43499 策略 |
| `execution.{heap,race,stages,consumer}` | backend | 43499 执行调参 |
| `route.{tcp_zerocopy,select_stack,multicast_waiter}.*` | backend | route 私有参数 |
| `execution.handoff.*` | `handoff` | 跨 backend 后处理调参 |
| `execution.recommended_cpus.*` | session/pipeline | 通用运行调参 |
| `meta.{kernel_major,recommend_shizuku}` | 待定 | 见开放决策 |

## 备选方案（Considered Options）

- **A. 维持单体表 + 单一 `kernel_offsets`。** 不采用：P6/扩展性/平台依赖问题原样保留。
- **B. 运行期自注册（backend 启动时注册）。** 不采用：引入可变全局/初始化顺序，且回到单例依赖。
- **C. 三端共享 schema 并 codegen（native/Kotlin/extractor）。** 暂缓：成本高；先用编译期 Schema + manifest +
  三端对拍测试，出现漂移再升级为 codegen。

## 后果（Consequences）

正面：
- profile 能表达任意 route/path 的任意参数，且**新 backend/新参数 = 加一个 Schema + 注册 + 测试**，不改容器；
- 平台与后端各取所需，DAG 不再有二义；P6 的“一处字段映射”有明确落点；
- 组件/route 合法性由 owner 校验，顺带打断 catalog↔backend 环；
- 生产 strict 拒绝未知字段，配置错误在启动期暴露。

负面/风险：
- `binary_profile::parse`/`serialize` 要重写为 framing-only；`profile_binary_test` 大改；
- Kotlin `NativeProfileDocument.sections()` 与 extractor `report.rs` 需按 owner 对齐并加 manifest 测试；
- 绑定代码在启动期（非 PI 窗口），但需确认不引入热路径间接/分配；
- wire 虽不变，`strict` 模式会拒绝旧工具产出的多余字段，需同步工具。

## 验证

- host：新增 `profile_schema_test`（跨 Schema `(section,key)` 查重、presence/宽度/必填、strict 未知拒绝、
  round-trip）；更新 `profile_binary_test`。
- Kotlin：`NativeProfileDocumentTest`/`NativeDocumentEquivalenceTest` 对 manifest 断言键名/宽度。
- extractor：输出键 ⊆ manifest（测试）。
- 门禁：wire 不变，绑定非攻击 8 函数 → `cmp_disasm` 不受影响；仍跑 host + NDK 零告警 + lint。

## 开放决策

1. `meta.kernel_major`/`meta.recommend_shizuku` 归属（platform / handoff / 中性元数据）。
2. 未知 section 是否一律拒绝（推荐）还是忽略。
3. manifest 形式（JSON/YAML/生成头）与是否把 Schema 作为唯一源 codegen 到三端。
4. `cred` 的 layout（platform）与 value（backend）在**同一 section** 内的拆分是否接受（按 key 归属；section
   可被多 owner 共享）。
