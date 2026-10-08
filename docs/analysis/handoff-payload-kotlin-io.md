# Kotlin 面：编解码边界 / 导入通道 / strings 登记（细则）

> 分析类（≤8 KB）。配套 [kotlin](handoff-payload-kotlin.md)（设计主表）、[b1b2](handoff-payload-b1b2-runtime.md) §B2.5、[plan](handoff-payload-plan.md) §10 D22/D27/D29、[ADR-0008](adr/0008-lua-runtime-sandbox.md)。**自包含，不引用任何外部计划**。

## 1. 编解码边界（R5）

- **常量同源**：native 侧 `glkv3::kMaxBinBytes`（新增于 `src/core/profile/glkv3.hpp`，与 `kMaxStringBytes = 256`（`:84`）并列）；Kotlin 侧在 `profile-core/src/main/kotlin/com/ghostlock/app/data/profile/Glkv3Encoder.kt:112`（`MAX_STRING_BYTES`）旁新增 `MAX_BIN_BYTES`，注释注明**镜像 native 常量**。
- **编码拒绝**：`Glkv3Encoder.writeValue` 的 `Glkv3Value.Bin` 分支（`:202-205`，现无上限）⇒ 增 `require(value.value.size <= MAX_BIN_BYTES)`，**超限即编码失败（具名）**，不产出文档。
- **解码上限**：`Glkv3Decoder` 的 `is BinaryValue -> Glkv3Value.Bin(...)`（`:117`，现无上限）⇒ 超限**返回 null（拒绝文档）**，与 native 的 fail-closed 一致。
- **边界用例**：`Glkv3EncoderTest` / `Glkv3DecoderTest`（profile-core 侧）各增两条：`size == MAX_BIN_BYTES` 通过、`+1` 拒绝；`Glkv3Golden` 不变（无脚本用例逐字节不变）。
- **与 native 的对拍（B5：机器对拍为必需，不是人工检查）**：① **成员表逐项同序**：native `QueueElementShapeTest` + Kotlin `QueueElementShapeAgreementTest`，两侧都必须绿（`make -C src native-host-tests` 与 `./gradlew :profile-core:test --tests "*QueueElementShape*"`）；② **常量对拍**：`kMaxMapMembers` / `MAX_BIN_BYTES` 写进 `make -C src glkv3-golden-hex` 产物**头部注释**，用例断言两侧相等；③ **边界向量**：两侧各跑「=MAX / +1」用例，期望一致（超限 ⇒ 具名拒绝）。

## 2. wire 键路径（C2a 裁决）

- **payload 键统一落 `backend.<id>.payload.*`**：**不开新顶层 owner**（顶层仍只 `backend.<id>`；`platform.*` 在迁移中）；`payload` owner **现为注释态（出现即拒）** ⇒ **解冻批次一并恢复该 owner 的 backend 作用域键**。
- **完整 section.key 与元素字段**：
  - `backend.<id>.payload.scripts[]`：元素 = `id`(str) · `sha256`(str, 64 hex) · `bytes`(**bin**，插入段 ≤64 KB) · `always` **不是**元素字段（它是**条目属性**）。**`argv` 已删除**：调用参数由**模板与 op** 决定，**不由用户传 `argv`**。
  - `backend.<id>.payload.modules[]`：元素 = `path`(str) · `sha256`(str) · `require_bypass`(bool，可缺省)；**上限 8**。
- **manifest 处置**：**既有行不变；以上新键按新行补充**（预期，不是「空 diff」）；58 份 golden 因 **presence-gated** 仍逐字节不变（无 payload 键的用例）。

## 2. 导入通道（R6）

- **现状**：`app/src/main/kotlin/com/ghostlock/app/data/payload/PayloadImportService.kt:21-24` 的 `enum class PayloadKind` = `Script("script", 4 MiB, ".sh")`、`Ko("ko", 64 MiB, ".ko")`。
- **新增并存**：`LuaScript("lua", 64 * 1024L, ".lua")`——**与既有 `Script` 并存**（不替换、不改其限值）。
- **用途区分（C2c 裁决）**：**wire 只承载「用户插入段」**（≤64 KB，走 `bin`）；**最终 `.sh` 由 native 用内置模板合成**（`terminal/root_script.cpp:15` 为 **12 KiB 生成缓冲**；**pin 产物 6096 B**：`src/core/tests/data/root_script_pin/safe_mode_{off,on}.sh`；App 只提供插入段与参数）。`PayloadKind.Script` 的 **4 MiB 仅是导入/存储上限**，**与 wire 无关**——**两个上限用途不同**（4 MiB = 导入存储；64 KB = wire 承载），不得混写为同一事实。**>64 KB 插入段 ⇒ 具名拒绝**（wire 是有界通道，理由写在此处与 ADR-0008）。
- **限值来源**：`kMaxBinBytes = 64 KB`（ADR-0008 / 本文件 §1，与 I13 的「执行期硬约束」同值）；App 侧不得放宽。
- **UTF-8 校验为新增项**：现 `PayloadImportService.import()`（`PayloadImportService.kt:47-60`）只做 **digest + copy**（有界哈希 + 复制），**无编码校验** ⇒ `LuaScript` 导入**必须新增 UTF-8 校验**（非法编码 ⇒ 具名拒绝）。
- **导入失败归因（具名）**：超限 / 非 UTF-8 / 扩展名不符 ⇒ 逐条具名失败；**区分两个 importer**——`PayloadImportService`（payload/`LuaScript`：no-backup 目录 + 有界哈希 + 复制）与 **plugin importer**（描述符 + 探针 + 校验）；**规则不共用**，脚本侧不得引用 plugin 的信任判定。
- **与 `ScriptStore` 的关系**：`PayloadImportService` 只负责「**拷贝 + 限值 + 哈希**」；`ScriptStore` **消费**导入结果做**登记与展示**（`ScriptTrustView`：作者/来源/sha256/大小/原语清单/能力/预算），**不做信任判定**（D27 L2 只展示不拦截）。

## 3. strings 登记（R9 / K-13）

**10 条 `GateReason`（与 b1b2 §B1.2 同名，含 `executor-unavailable`）逐条登记**：

| reason | string 资源（`app/src/main/res/values/strings.xml`） | 展示口径 |
|---|---|---|
| `unknown-op` | `gate_unknown_op` | 「未知操作」+ path/index |
| `op-not-available` | `gate_op_not_available` | 「该设备/后端不提供此操作」 |
| `route-not-available` | `gate_route_not_available` | 「路径不可用」 |
| `geometry-missing` | `gate_geometry_missing` | 「缺少所需几何（配置缺字段）」 |
| `capability-missing` | `gate_capability_missing` | 「设备能力不足」 |
| `order-violation` | `gate_order_violation` | 「顺序/依赖不满足」 |
| `hash-mismatch` | `gate_hash_mismatch` | 「清单哈希不匹配」 |
| `param-invalid` | `gate_param_invalid` | 「参数类型/取值不合法」 |
| `seam-in-pi-window` | `gate_seam_in_pi_window` | 「seam 位置非法」 |

**L2 展示字段（`ScriptTrustView`）逐条登记**：作者 `script_author` · 来源 `script_source` · sha256 `script_sha256` · 大小 `script_size` · 原语清单 `script_primitives` · 能力 `script_caps` · 预算 `script_budget`。

- **同步要求**：新增 string 必须同时更新 `app/src/test/kotlin/com/ghostlock/app/ui/MessageResIdGuardTest.kt` 的断言（**UI 不得硬编码文案**，一律走资源 id）。
- **覆盖范围现状**：该测试**当前只走 plugin/payload 投影**（`MessageResIdGuardTest.kt:11-20`）⇒ 本清单要求**扩展到 handoff 页**（`GateReason` 10 条（含 `executor-unavailable`） + L2 展示 7 字段 + 计划页核心文案）；未登记资源的新行即失败。
- **口径**：`dirty-failure` **不是** gate 原因（运行期终止语义），因此**不登记**为 gate string；如需提示，另起运行期诊断文案。

## 4. 未决与边界

- 预置脚本 assets 目录（`app/src/main/assets/payload/`）**尚未创建**：本文件只登记设计位置；创建与资产落地属实现批次（导出集合边界见 [kotlin](handoff-payload-kotlin.md) §8）。
- `MAX_BIN_BYTES` 的最终数值以 native `kMaxBinBytes` 为准；两侧不一致属**对拍失败**（用例红）。
