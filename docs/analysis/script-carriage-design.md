# 计划脚本的载具设计（wire 携带生效脚本）· **v2（按 CAR-1..7 改）**

> 状态：**草稿待三审** · 层级 **L** · 作者 Lead · v1 的二审见 design-critic 报告（CAR-1 阻断 + CAR-2..7）。
> 上级权威：`docs/analysis/script-plan-design.md`（**方案 C**，用户 2026-10-08 裁定）· 本文只回答它第 5 条留下的一个问题。

## 0 v2 改了什么（对二审的逐条处置）

- **逐条处置表已迁出**（配额）⇒ 见 **[script-carriage-design-history.md](script-carriage-design-history.md)**（CAR-1..7 全文，内容未改）。

## 1 决定（四条）

1. **载体 = payload 家族**：`backend.<backend>.payload.<name>.bin/sha256/size/kind` ✓，其中 **kind 取词表的 `plan`** ✓、**脚本源码 = `.bin` 字节** ✓、**`.size` = 字节数** ✓、**`.sha256` = 其摘要** ✓。
2. **名称 = 终端 token + `_plan` 后缀** ✓（家族规则：名字以**已声明的 terminal token** 开头，后缀 ≤16 字符 ✓ ⇒ 名字可枚举 ✓；`payload_keys.hpp:92-104` 明写**后缀必填**、`payload_keys_test.cpp:61-69` 与双侧 pin `payload-manifest.pin.tsv:12` 的 `bare_name rejected` 共同钉住 ⇒ **本设计沿用它，不改名规则** ✓）。语义：**该终端要执行的计划脚本** ✓。**名字只借用 terminal token 作为可枚举命名空间，不表示归属** ✓（链仍由根标量定 ✓）；同一 terminal 可同时持有收尾 payload 与 plan（`payload_name_is_valid` 已按后缀区分 ✓）。每个文档**至多一份生效脚本** ✓ —— 内置件按 (backend, route) 由 **App 挑选**后写入此名 ✓（见 §4.1）。
3. **上限复用 `kScriptSizeCeiling`（64 KiB）** ✓；超限 ⇒ 具名拒绝 **`budget-exceeded`** ✓（不截断 ✓）。**落点（R3 处置）**：该常量现只在 `script/sandbox.hpp:39` 定义、`script/` 内使用，而 `profile` 与 `script` 之间**当前无 include** ⇒ 为免造第二个数、也免未证的分层依赖，**把常量提升到 `contract/script_surface.hpp`**（两层都已 include contract ✓），删除 sandbox 内的旧定义，只留一处定义 ✓。**并写清归属（R2）**：家族只校验 `size` 键 == `bin` 长度 ✓（`glkv3_document.cpp:366-370`），**64 KiB 是 loader 的判据** ✓ —— 超限由 loader 报 `budget-exceeded` ✓。
4. **native 只见有/无** ✓：有 `plan` 交付物 ⇒ 用之；**无 ⇒ `plan-missing`** ✓。**摘要验证落点（R1）**：`Document::payload()` 只校验摘要的**文本形式** ✗（`glkv3_document.cpp:346-349`），**重算**发生在 `terminal::check_payload()` ✓（`terminal/payload.hpp:61-70`）⇒ 计划脚本在 **stage 期**消费、**不经该函数** ⇒ 本设计规定：**loader 在暂存脚本前调用同一 `check_payload()`（或等价重算 + 文本比对）** ✓，不符 ⇒ 沿用 `payload-missing` ✓（与执行侧「三码归一」口径一致 ✓）；否则 sha256 键就是**装饰键** ✗。native **不建内置脚本地图** ✓（那是「名字 → 值表」的近亲 ✗）；「内置件也没有」是 **App 侧条件** ✓，见 §4.4。

## 2 为什么不是根级标量、也不是段（回应二审 ⓐ）

- 根级 `str` ✗：**并行的第二载具** ✗（一事实一载具 ✓）；且 `root_text` 无长度出参、超限与缺失同码 ✗（二审实测 ✓）—— 这两条**都不是偏好问题，是能力与纪律问题** ✓；
- `plan.script` 段 ✗：同样是第二载具 ✗；
- payload 家族 ✓：长度显式 ✓ · 三键一致性已校验 ✓ · 名字空间可枚举 ✓ · kind 词表已含 `plan` ✓ —— **零新增载体** ✓。

## 3 与既有规则的相容性核对

| 既有规则 | 本设计 |
|---|---|
| 一事实一载具（M5 教训） | 相容 ✓（复用既有家族，不新增第二载具） |
| 选择与策略不得出现在 CLI | 相容 ✓（脚本随文档来） |
| 禁止名字 → 值表 | 相容 ✓（文件名是**载体键**而非名字到值的映射；native 不落内置表） |
| 脚本是顺序的唯一权威 | 相容 ✓（queue 退役后本交付物是顺序的唯一来源） |
| 上限不得自造第二个常量 | 相容 ✓（复用 `kScriptSizeCeiling`） |

## 4 影响面（B1 同批）

### 4.1 App 侧（内置件）

资产 `plan-<backend>-<route>.lua` **每个已接线的 (backend, route) 一份** ✓，集合**由已接线的组合集派生**（不手抄 ✗）—— 派生源点名为 **`combination-manifest.tsv` 的运行时副本**（`profile-core/src/main/resources/` ✓，AGENTS 既有对拍物 ✓），并**双侧对拍**；**缺件 ⇒ App 拒绝发射** ✓（判据落在 Kotlin 侧测试 ✓；若漏到运行期，native 只会报 `plan-missing` ✓）。发射时把选中的那份写入 `.bin` ✓。**代价须可见**：每份文档各带一份脚本 ⇒ 文本体积重复 ✓（可接受，但要写明 ✓）。

### 4.2 native 侧

`profile/glkv3_document` 已有 `payload()` ✓ ⇒ 新增的只是**计划解析**：读 kind 为 `plan` 的交付物 ⇒ 校验摘要与尺寸（家族既有 ✓）⇒ **删除 queue 路径** ✓；`plan-missing` 语义**收缩**为「文档未携带 plan 交付物」✓。

### 4.3 step_set / StepId 连带退役（D3 的接受项，本件补全）

`contract/step_catalog.hpp`（`StepId`/`step_set`/别名表）· `stepset-steps.tsv` **双副本各 14 行** + 其 manifest 测试与 Makefile 目标 · `vocabulary-manifest` 的 stepset 行 · AGENTS 的「stepset → 步骤序列 manifest」条目 · `run_plan.hpp` 的 `step_set_for_steps`/`StepSetUnknown`/`kMaxRunSteps` ✓。

### 4.4 语义归属（避免同一事实两处定义）

native 的 `plan-missing` = **文档未携带 plan 交付物** ✓；裁定第 5 条的「又无内置件」= **App 侧条件** ✓（App 必须总能挑到一份，否则它自己拒绝发射 ✓）。⇒ 同批同步 `composition-layer-p4-spec.md` 的措辞 ✓。

### 4.5 对拍与重冻

**本设计不改 payload 家族的 pin 与命名规则** ✓（`src/core/tests/payload-manifest.pin.tsv`，12 行 = segment / 2 terminals / 4 keys / 3 kinds / suffix_max / bare_name ✓，由 native `payload_manifest_test.cpp:22` 与 Kotlin `HandoffPayloadTest.kt:114` **两侧**读取 ✓）；若将来需新增 kind/名字模式 ⇒ **同批改 pin 双侧 + 重冻** ✓。对拍物：`profile-manifest-v3.tsv` 双副本（新增 payload 行的**枚举规则**说明 ✓，行数按实出 ✓）· `glkv3-native-fixture.tsv` · `Glkv3Golden.kt` · `native-doc-golden-v3.sha256` —— 全部以生成命令实出为准 ✓。

### 4.6 判据（可执行 + 边界值）

| 用例 | 期望 |
|---|---|
| `make -C src check` | EXIT=0 ✓ |
| 正常脚本 | 通过 + 计划可解析 ✓ |
| **恰好 64 KiB** | **通过** ✓ |
| **64 KiB + 1** | 具名 `budget-exceeded` ✓（EXIT ≠ 0） |
| **空脚本（size=0）** | 具名 `plan-missing` ✓（有交付物但无计划 ⇒ 不是计划） |
| 无 plan 交付物 | 具名 `plan-missing` ✓ |
| **控制组（证伪）** | 去掉「超限判定」⇒ 第三条用例必须红 ✓ |

> 脚本调用如何下落为 op（三条规则）见 [op-fall-rule](op-fall-rule.md)。

## 5 待评审问题

1. 名称 `<terminal>_plan` 是否恰当（家族规则要求以 terminal token 开头；是否应改为裸 terminal token）？
2. 上限复用 `kScriptSizeCeiling` 是否成立（sandbox 的上限是**脚本大小** ✓，此处同为脚本 ✓）？
3. §4.6 的空脚本语义（`plan-missing` 而非 `param-invalid`）是否成立？
