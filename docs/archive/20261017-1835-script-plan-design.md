> 归档说明（2026-10-17）：归档原因 = **载体已删除**（Lua / GLKv3 文档 / script 面 / pipeline-terminator / nlohmann wire_doc / 访问能力层 —— 见 docs/plan/MASTER-PLAN.md §0.1），本文仅作历史记录。原始路径 = docs/analysis/script-plan-design.md；归档路径 = docs/archive/20261017-1835-script-plan-design.md。

# 计划脚本设计（queue 退役 · 选 C）

> **2026-10-17 作废**：本文的载体（Lua 沙箱、script/wire 面、GLKv3 文档解析、pipeline/terminal 编排、nlohmann wire_doc）已从源码与构建中删除，
> 内容仅作历史记录；现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

> 分析类（≤8 KiB）。**L 级设计**（wire/profile 格式 + 跨语言契约）：**必须先获用户批准，才可动代码**。**自包含**。
> 裁定来源：用户 **2026-10-08「选 C —— queue 退役，计划唯一载体 = 脚本」**；理由：有脚本就不需要 queue，两套机制并存违反「取代而非叠加」。

## 1. 决策（五条）

1. **保留 `backend.<id>.route`**：它是**编译期策略与几何选择**，**不是步骤**。
2. **删除 `backend.<id>.queue`**：**出现即拒**，沿用既有具名拒绝风格（M5 的 `token-form-removed` 同款），`reason = queue-removed`，附 `path` 与 hint。
3. **计划 = 脚本**：**脚本是顺序的唯一权威**；gate 不做顺序推断、不读第二处顺序来源。
4. **内置计划脚本**：每个 **(backend, route)** 一份随 App 资产提供（命名建议 `plan-<backend>-<route>.lua`），使**不带自定义脚本也能跑**。
5. **文档自包含**：生效脚本**随文档发射**（wire 携带脚本）；既无脚本又无内置件 ⇒ 具名拒绝 **`plan-missing`**。

## 2. 影响面清单（实测）

| 项 | 现状（实测） | 目标 | 批次 |
|---|---|---|---|
| **资产** | **67/72** 个 `.conf` 含 `queue`（`grep -rl queue app/src/main/assets/profile/*.conf \| wc -l` = 67） | 删除 queue 段，改由内置计划脚本或自定义脚本表达顺序 | B1 |
| **Kotlin** | **8** 文件：`data/ProfileLayout.kt`、`data/NativeProfile.kt`、`data/profile/NativeProfileGlkv3Adapter.kt`、`data/profile/ProfileExporter.kt`、`data/component/StepSequenceCatalog.kt`、`ui/ExecutionCombinationPresentation.kt`、`data/AndroidProfileConfigController.kt`、`data/Profile.kt` | 停止发射/解析 queue；改为脚本引用与展示 | B1 |
| **测试与对拍** | **37** 文件（native `core/src/tests` 13 + Kotlin 24；按含 `queue` 统计） | 改为 `queue-removed` / 脚本路径断言，并重冻 | B1 |
| **native** | `glkv3_parse.cpp` 的 `resolve_queue`（`:266-273` 起）与相关具名诊断 | 删除解析路径，改为「出现即拒」 | B1 |
| **manifest / golden / fixture / native-doc-golden** | 均含 queue 相关行/字节 | **重冻**（逐项清单见 §2.1） | B1 |
| **extractor 产出侧** | **待核**：是否发射 `queue` | 核后决定「不发射」或同批删除 | B0.0 核实 |
| **文档** | queue-schema 为现行权威 | 头部加**退役指针**（原文保留作沿革）+ 本文成为新权威 | B0.0 |

### 2.1 重冻清单（逐项 file:line + 命令；**以生成器输出为准**）

| 资产 | 位置（file:line） | 命令 |
|---|---|---|
| `profile-manifest-v3.tsv`（双副本；**110 → 108 行**） | `app/src/test/resources:18/:92` + `profile-core/src/main/resources:18/:92` | `make -C core profile-manifest` |
| `glkv3-native-fixture.tsv`（**实测 90 行**；旧版误写 91） | `:12` | `make -C core glkv3-golden-fixture` |
| `Glkv3Golden.kt`（`SELECTION_HEX` = **3920**；旧值 3964 为 M5 前） | `:17/:20` | `make -C core glkv3-golden-hex` |
| `native-doc-golden-v3.sha256`（全 58 行） | 整文件 | Kotlin exporter 重生成（先例 `device-gates/s4-r2-20261005-pass.md:67`） |
| `stepset-steps.tsv`（双副本；`:8` 仍提 step token） | `app/src/test/resources` + `profile-core/src/main/resources` | `make -C core stepset-steps-manifest` |
| **67 个 HOCON 资产** | `assets/profile/*.conf` | Kotlin 迁移 + `./gradlew exportProfiles` |

- **无影响（命中 0）**：`combination-*` · `vocabulary-manifest.tsv` · `lkm-kmi-manifest.tsv` · `plugin-probe-*` · `plugin-wire-shape-golden.bin`。

### 2.2 native 删除面（补全）

- `document.hpp`（`Composite*`/`DeclaredArrayField`/`kDeclaredArrayFields:88-89`）· `glkv3.hpp`+`glkv3.cpp`（array/map 解码）· `schema.hpp`（`selection_owned:222-225`/`WireKind::Array:244-249`/`bind_all:432-460`）。
- backend schema：43499 `schema.hpp:175-185` + `glkv3_schema.hpp:99-104`；43284 `schema.hpp:165-172` + `glkv3_schema.hpp:34-36`。
- 43284 `execution_binding.cpp:297-313` + `backend_terminal.cpp:191-194`。
- **`contract/step_plan.hpp` 全量**（`:38-42` `kMaxQueueSteps/kMaxQueueSeams`、`:82-91` reason、`:110-141` `QueueElement/QueueInput/SeamSlot/CanonicalPlan`、`:222` `normalize_step_queue`）。
- `contract/step_catalog.hpp:59-77/:124-135` · `cve_2026_43499/backend_profile.cpp:14-15/:82-85`（**仍读已退休 steps 键**）· `orchestrator.hpp:32/:50/:84` · `component_catalog.hpp:200` · `Makefile:180/:548-551`。
- **测试 ~15 文件**：`step_plan_test`（53）· `queue_wire_test`（53）· `profile_string_binding_test`（7）· `combination_token_test`（6）· `43284_backend_terminal_test`（4）· `glkv3_schema_test`（3）· 另 5 文件各 1。

### 2.3 联动项：extractor ↔ manifest 对拍（**必须同批**）

- `tools/extract_rs/src/report.rs:185` **仍发射已退休的** `backend.cve_2026_43499.steps`；`:1745` 测试断言「**extractor 可发射键 ⊆ native manifest**」。
- ⇒ 删 manifest/声明中的 `steps`/`queue` 行时，**必须同批**改 `report.rs:185` 与该测试，否则**对拍必红**。**建议同批清 steps 键**。

## 3. 能力与诚实边界（**不承诺**项同样重要）

- 顺序**不可静态判定** ⇒ gate 期**只能**保证：**op 名合法** · **参数合法** · **能力上界** · **预算内终止** · **窗口内零 Lua**。
- **不承诺**：脚本会按「正确顺序」调用（无法证明）；**这是本设计的已知残余风险**。
- 缓解：**内置计划脚本**（默认路径可用）+ L2 **只展示不拦截**（用户自检）。

### 3.1 窗口内可变性：一律参数化（回应用户）

- **窗口内可变性一律参数化**：先例 = `execution-tuning.conf:20-36`（`stages`/`handoff` 两组键）+ `steps.cpp:77/:238` 的 attempts 循环。
- **拆分窗口不提供额外自定义**，只带来**四条代价**：① 窗口内 Lua；② 窗口内新增代码路径；③ 窗口拉长；④ 失败态组合爆炸。
- 参数**上界由 `ParamSpec.max` 强制**（超界 ⇒ 具名拒绝）。

## 4. 窗口原子性

- 窗口内多步（park → write×N → release）**必须是一个 op**（不可拆成多个脚本可见步骤）。
- 据此脚本只能编排 **op 之间**：与 `handoff-payload-b1b2-runtime.md:78`「landing 只能声明在**操作之间**」及 ADR-0008 的窗口约束一致。

## 5. 单一权威校验

- **不得存在第二条顺序读取路径**：白盒判据 = `queue` 残留 **grep = 0** + **无第二处顺序常量**（如同一顺序在 Kotlin 与 native 各写一份）。

## 6. 判据（黑盒 / 白盒 + 极端值）

- **黑盒**：带 `queue` 的文档 ⇒ 具名 `queue-removed`；缺脚本且无内置件 ⇒ `plan-missing`；正常文档 ⇒ **逐字节可复现**的 wire。
- **白盒**：`queue` 残留 = 0；op 上界校验单测；无第二处顺序来源。
- **极端值**：空脚本 · 语法错 · 超预算 · **文档尺寸上限**（实测 `kMaxDocumentBytes = 1 MiB`，`glkv3.hpp:87`）· 引用不存在的 op · 表超深 · **脚本内 queue 残留**。

> 口径说明：文档中出现的「896 KB 上限」在仓内**查无出处**（`grep -rn 896 core/src/profile` = 0）；现行上限以 `kMaxDocumentBytes`（1 MiB）为准。

## 7. 批次归属

| 批次 | 内容 | 门禁 |
|---|---|---|
| **B0.0** | 本设计 + **用户批准**（前置）；extractor 产出侧核实 | 用户批准 |
| **B1** | wire/manifest/golden/fixture 重冻 + native 删除 queue（改具名拒绝）+ **内置脚本通道** | G1 |
| **B2** | Lua 运行期 + UI（查看 / 替换脚本） | G1（若触写路径 ⇒ G2） |

- **动攻击路径：否**；但本设计改 **wire/契约** ⇒ 相关批次若触及写路径，按现行规则走**真机门禁**。

## 8. 沿革

- **token 白名单**（—2026-10-06）→ **queue**（2026-10-06）→ **script**（**2026-10-08**，本设计）。
- 方向一致：**选择面从白名单走向可编程**。
- 旧结论**保留**：[queue-schema](handoff-payload-queue-schema.md) 原文**不删**，仅头部加退役指针。
