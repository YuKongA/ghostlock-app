> 归档说明（2026-10-17）：归档原因 = **载体已删除**（Lua / GLKv3 文档 / script 面 / pipeline-terminator / nlohmann wire_doc / 访问能力层 —— 见 docs/plan/MASTER-PLAN.md §0.1），本文仅作历史记录。原始路径 = docs/analysis/lua-config-structure-options.md；归档路径 = docs/archive/20261017-1835-lua-config-structure-options.md。

# Lua 配置结构选项（R10 决策简报）

> **2026-10-17 作废**：本文的载体（Lua 沙箱、script/wire 面、GLKv3 文档解析、pipeline/terminal 编排、nlohmann wire_doc）已从源码与构建中删除，
> 内容仅作历史记录；现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

> 分析类（≤8 KiB）。**用途**：为 R10（Lua 配置结构设计）提供**事实底稿 + 三形态对比 + 判据/分期**，供用户裁定；**本文不替用户选形态**。
>
> **取代声明（2026-10-10）**：用户裁定「全 lua 配置文件」⇒ 方向为 **L-A（脚本即配置）**。本简报的三形态对比**已被** [config-wire-redesign-plan.md](config-wire-redesign-plan.md) **取代**；其 §6 两隐患与 §4 判据已并入该设计。本文保留为历史输入（不删历史）。
> 配套：[B0.0-REVISION](../plan/B0.0-REVISION.md)（R3/R10 台账）· [handoff-payload-verification](handoff-payload-verification.md) §1（判据形式）· [handoff-plugin-lua](handoff-plugin-lua.md)（控制面）。

## §1 四层默认值事实底稿（每条 file:line）

| 层 | 位置 | 现状（谁在填默认值 / 怎么填） |
|---|---|---|
| **① 资产层** | `app/src/main/assets/profile/`：`execution-tuning.conf`、`execution-tcp-zerocopy.conf`、`execution-select-stack.conf`；per-release `*.conf` 用 `include` 片段组合 | 静态调参写死在资产里；**per-route 只有 tcp/select 两份调参文件，multicast 没有**（真机已验证的组合靠公共段 + 内置值） |
| **② Kotlin 层** | `ProfileMerger.kt:20-25`（合并顺序 **builtin → imported → overrides**，deep merge）；`:79 fillRouteExecutionDefaults(...)`（**主动注入 route 执行默认值**）；`NativeProfile.kt:417`（route wire `?: 0u`）、`:450/:451`（`vu/vul` 取值 `?: 0u`/`?: 0uL`）、`:503`（derived route kind `?: 0u`） | **两套填默认机制**：合并器显式注入 + 访问器**零填充**；缺失键在 Kotlin 侧**不会报错** |
| **③ manifest 层** | `app/src/test/resources/profile-manifest-v3.tsv`（列序：`owner path wire required default source doc width`）；`NativeProfileGlkv3Adapter.kt:49-70`（**只把 `width` 当硬校验输入**）、`:92-93`（width 缺失即可拒） | `default` 列**只作为导出信息存在**，native/Kotlin **没有把它当取值来源**；`required` 列在 Kotlin 侧也未见强制消费 |
| **④ native 层** | `core/src/profile/schema.hpp:36-46`（`BindMode::Production` fail-closed：未知段/键 + **缺失 required ⇒ 拒绝**；`BindMode::Test` **放宽 required**）、`:94-100`（`BindCode::MissingRequired` → `missing_required`）；`core/src/contract/model.hpp:302-304`（`race_route_done_timeout_ms` 走 `or_default(…, 300000)`）、`:378-386`（`or_default` 实现；`:386` 未加载时再兜 `0`） | native 有**具名拒绝**能力，但只对声明为 required 的字段生效；accessor 仍保留 `or_default` 兜底（**3 处**） |

**一句话**：默认值分散**四层**；Kotlin 的「零填充 + 注入」与 native 的 `or_default` 会**掩盖缺失**，manifest 的 `required/default` 列在两端都**不是执行期权威**。

## §2 改为必填会波及的三组影响面

| 组 | 影响面（只列，不改） | 关键风险 |
|---|---|---|
| **G-1 资产** | 每个 route/backend 都必须有可提供全部必填键的资产；`execution-*` 调参文件需**逐一补齐**；per-release `*.conf` 与 include 片段要复核 | **multicast 目前没有 tuning 资产** ⇒ 一旦全表必填，**真机已验证的 multicast 组合会被拒**（见 §6-隐患 B） |
| **G-2 Kotlin** | `ProfileMerger.fillRouteExecutionDefaults`（注入点）与 `NativeProfile` 的 `?: 0u`/`vu`/`vul` 零填充**必须同批改**；涉及 `:app:testDebugUnitTest` 与既有 Kotlin 对拍 | 只改 manifest 不改这两处 ⇒ **零填充继续掩盖缺失**，required 形同虚设（见 §6-隐患 A） |
| **G-3 manifest + native** | `default` 列语义作废需同步**导出器**（`make -C core profile-manifest-v3`）与对拍（`core/src/tests/profile_manifest_v3_test.cpp:69`、`ProfileManifestV3AgreementTest`）；native 侧 `or_default`（`model.hpp` 3 处）收紧、`BindMode::Test` 的 required 放宽不得用于掩盖真实缺失 | 两侧不一致 ⇒ **同一事实两处定义**（对拍会红，但需同批更新 golden） |

## §3 三形态 × 四维对比

| 维度 | **L-A 单脚本返回表** | **L-B 脚本 + 显式 schema** | **L-C 数据优先 + 脚本扩展点** |
|---|---|---|---|
| **默认值放哪** | 脚本内（或脚本返回表缺省处）；Kotlin 零填充仍需去掉 | schema 声明默认/必填，gate 期校验；默认值集中在 schema | 数据（HOCON/wire）承载默认值，Lua 只覆盖；**默认值仍在资产层** |
| **include 与合并是否成立** | 成立（HOCON 侧不变）；但「脚本表」与「合并结果」需定义优先级 | 成立；schema 与合并结果对拍 | 成立（现状语义保留最好）；Lua 覆盖需声明回退 |
| **manifest 与对拍资产影响** | 中：`default` 列作废，对拍 golden 需重导 | 大：schema 与 manifest 需**同源**（否则两处定义） | 小：资产/合并语义不变，仅新增覆盖通道 |
| **UI（App 预设与编辑器）改动面** | 中：编辑器要能编辑脚本表 | 大：编辑器要理解 schema（表单可生成） | 小：沿用现有字段编辑器，仅增加「Lua 覆盖」入口 |

- **改动面最小**（当前代码为准）：**L-C**（资产/合并/编辑器几乎不动；风险集中在「双读取路径」需对拍）。
- **对用户最灵活**：**L-A**（脚本即配置）；**L-B** 次之（受 schema 校验约束）。

## §4 判据与门禁（裁定后落地时用）

- **黑盒**：同一意图在选定形态下产出**逐字节可复现**的 wire（同一输入两次导出逐字节相同）；缺失必填键 ⇒ native 具名拒绝（`missing_required`，`path` 精确到键）。
- **白盒**：Kotlin 侧 `?: 0u` / `fillRouteExecutionDefaults` 与 native `or_default` **不再掩盖缺失**（grep 计数为 0，或仅保留在明确标注的兼容路径）；无第二条读取路径（或已声明回退 + **逐值等价对拍**，M5 教训）。
- **极端值**：空脚本 / 语法错 / 超指令预算 / 引用不存在参数 / 表深度超限；资产缺一个必填键 / 多一个未知键；uint64 边界与 width 不符。
- **门禁**：**G1**（`make -C core native-host-tests` + `./gradlew :app:testDebugUnitTest` + `./gradlew markdownlint`）+ **用户裁定**（形态含产品取舍）；**不触攻击路径 ⇒ 免真机**（若形态改为运行期动态装载而触及写路径，另行评估）。

## §5 建议与分期（建议，不代替裁定）

| 期 | 内容 | 判据 | 门禁 |
|---|---|---|---|
| **P0（先修隐患）** | 消除 §6 两隐患：Kotlin 零填充/注入与 native `or_default` 的「掩盖缺失」；multicast 调参资产补齐或显式豁免 | 白盒：零填充命中 = 0；黑盒：multicast 组合仍被接受 | G1 |
| **P1（定形态）** | 用户在 L-A/L-B/L-C 中裁定（含默认值归属与 include 语义） | 裁定书 + §3 四维结论 | 用户裁定 |
| **P2（落必填）** | 按 §2 三组影响面落地 R3（默认值列作废 + accessor 去默认 + 具名拒绝） | §4 判据全绿 | G1 |

- **顺序理由**：不先修隐患就改必填 ⇒ 被掩盖（假绿）；不先定形态就改 accessor ⇒ 重复返工。

## §6 与 R3 的关系 + 两个必须先修的隐患

**关系**：R3（无默认值 / 必填范围）**阻塞于 R10** —— L-B 倾向「全表必填」（schema 已声明），L-A/L-C 倾向「被选中路径必填」；**R10 裁定即关闭 R3**。

**隐患 A · `?: 0u` 零填充让 `required` 形同虚设**：`NativeProfile.kt:417`（route wire）、`:450/:451`（`vu`/`vul`）、`:503`（derived kind）在缺失时**静默填 0**；native 侧 `model.hpp:302-304`（`or_default(..., 300000)`）与 `:386`（未加载兜 `0`）同样兜底 ⇒ 即使 manifest 标 `required`，**缺失也到不了 `missing_required`**。

**隐患 B · multicast 无 tuning 资产会让必填拒掉真机已验证组合**：per-route 调参只有 `execution-tcp-zerocopy.conf` 与 `execution-select-stack.conf`；**`multicast_waiter` 无对应调参资产**（`multicast_waiter` 仅出现在 per-release `*.conf` 的 route 声明中）⇒ 若直接全表必填，**multicast 路径会被判缺失**，而该路径**已由开发者真机验证**（Multicast 5.15）。

⇒ **结论**：R3 落地前必须先修 A（去零填充/去兜底）与 B（补 multicast 资产或显式豁免），否则「必填」既无效（A）又有害（B）。
