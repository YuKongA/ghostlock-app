> 归档说明（2026-10-06）：归档原因 = **已实行**（S4-R2 implementation 记录，对应改动已落地；政策第 1 条：已实行的 implementation 记录一律归档）。证据：① 生产路径判据 —— contract/countermeasure.hpp 仍被 contract/step_plan.hpp:24-26 include（§九 例外 2 ⇒ 非过期），common.* / countermeasure.* owner 无生产写入者（backend/cve_2026_43499/schema.hpp:114 注明 vivo consumer 已删；tests/profile_v3_test.cpp:199/206 断言出现即拒）；② 命中点分类 —— L11/L22/L23 = 乙（common.*、countermeasure.vivo_vr_guard.* 已废）；L30/L38 = 丙（owner-qualified 段名与选区感知仍现行，其现行内容已由契约 §3.20 承载）。原始路径 = docs/analysis/s4-r2-implementation.md；归档路径 = ��

# S4 · R2 实施设计：wire 原地增量（owner-qualified + selection + string + manifest v4 + 选区感知）

> 上游权威：`config-wire-redesign-plan.md`（R2、§5.3、§6.1 重命名表）。前置：**R1 已完成**（`38f1343`，真机门禁 PASS）。

## 1. 目标

1. **owner-qualified 段名**（wire 原地改，`schema == 3` 不变）；
2. **选区感知文档**：只写「该 selection 拥有的段 + common」→ **43284 文档不再携带 43499/platform 的 12 个段**；
3. `string` 类型可用（UTF-8，上限 256；canonical 规则不变）；
4. **manifest v4**（owner 列驱动）+ Kotlin **生成式**映射（不再逐字段手写）；
5. `common.*` 收纳元信息/选择。

## 2. 重命名表（§6.1 权威，逐条照做）

| 现路径 | 目标路径 | 归属 |
|---|---|---|
| `route.<kind>.*`（18） | `backend.cve_2026_43499.route.<kind>.*` | 43499 私有（43284 无 route） |
| `execution.*`（24） | `backend.cve_2026_43499.execution.*` | 43499 私有（race/consumer/handoff/heap/stages） |
| `task_struct`（15） | `platform.abi.task_struct.*` | 平台 ABI 事实 |
| `cred`/`offset`/`kernel` 的**平台部分** | `platform.abi.{cred,offset,kernel}.*` | **消歧**（现与 43499 同名） |
| `cred` 的 **43499 模板部分** | `backend.cve_2026_43499.cred.*` | 43499 私有 |
| `meta.*`（4） | `common.*` | 公共（选择/元信息） |
| `vr_guard.*`（1） | `countermeasure.vivo_vr_guard.*` | 随 vivo 插件化（R4c，本批只改名不搬代码） |
| `backend.cve_2026_43284.*`（7） | **不变** | 已是正确形状（样板） |

## 3. 逐文件清单

**native**
- `src/core/platform/abi.hpp`：段名常量 → `platform.abi.*`（`task_struct`/`cred`/`offset`/`kernel` 的平台部分）；
- `src/core/backend/cve_2026_43499/schema.hpp`（+ `backend_profile/**`）：段名 → `backend.cve_2026_43499.*`（含 `route.*`/`execution.*`）与 `common.*`；
   `cred` 的 43499 模板字段归 `backend.cve_2026_43499.cred.*`；
- `src/core/backend/cve_2026_43284/schema.hpp`：**不动**（`backend.cve_2026_43284.*`）；
- `src/core/profile/{schema.hpp,registry.hpp,glkv3_parse.cpp}`：段名解析按 owner 前缀校验（未知 owner 前缀 → 拒绝）；`bind_all` 的**选区感知**：只绑定该 selection 的 owner 集合；
- `src/core/profile/binary.cpp`（v2 只读）：**保持** legacy Field 表（R1 一致声明），不得因改名破坏 v2 读取；
- 测试：`profile_manifest_v3_test.cpp` + `app/src/test/resources/profile-manifest-v3.tsv`（重生成）、`profile_registry_test`、各 backend schema 测试。

**Kotlin / profile-core**
- `NativeProfileDocument`：**选区感知**——按 selection 只写 own 段 + `common.*`；路径按上表 owner-qualified；
- `NativeProfileGlkv3Adapter`：改为**由 manifest 生成**的映射（不再逐字段手写）；
- `Glkv3Encoder`：支持 `string`（UTF-8；canonical 不变）；
- route 目录：`RouteKind` 改为**每 backend 一份**（43499 私有）；
- 测试：`ProfileManifestV3AgreementTest`、`NativeProfileGlkv3AdapterTest`、`ChannelBStdinTest` 同步。

## 4. 不变量

- `schema == 3` 不变；canonical 编码不变；**v2 只读兼容不变**；
- **43284 文档变小是目标**，但 43284 的**输入值**（`steps`/7 个字段 + release + selection）逐项不变；
- 43499 真机行为不变（值相同，路径换了 owner）；
- 无新增可变全局；零告警；防火墙不变；
- **跨语言原子性**：Kotlin 与 native 必须同批落地（同一分支内版本绑定，不做双轨长期兼容）。

## 5. 门禁

| 门槛 | 命令 |
|---|---|
| host | `make -C src native-host-tests`（含防火墙 + 两份 manifest 测试） |
| NDK / lint | 零告警 / 0 findings |
| Kotlin | `:app:testDebugUnitTest`、`:profile-core:test`（含 manifest 对拍） |
| 真机 43499 | 冷启 + `--load-prebuilt-profile` → root + KernelSU ready |
| 真机 43284 | app-call（adb 试验台）→ EXIT=0 + `profile_resolved` 值与 R1 一致 + `lkm_window opened=1` |
| 证据 | 43284 文档段数由 13 → **仅 own + common**（打印段列表对照） |

## 6. 风险

- **段名是跨语言契约**：漏改一侧 → 绑定期直接拒（fail-closed，但会让 App 起不来）→ 必须同批 + 三端对拍；
- manifest 重生成会牵动 `profile_manifest*.tsv` 与 Kotlin 对拍测试，需一次性对齐；
- v2 只读路径的 legacy Field 表不要动（否则破坏 v2 兼容）。

## 归档评估（2026-10-06，批次 3）

- **判定：丙（混合）⇒ 留住，不归档**（本批不改原正文）
- **命中点与分类（行号）**：
  - L11「common.* 收纳元信息/选择」⇒ **乙片段**：common owner **已删除（出现即拒）** ⇒ 该设计已不成立；
  - L22「meta.* → common.*」⇒ **乙片段**（同上）；
  - L23「vr_guard.* → countermeasure.vivo_vr_guard.*（随 vivo 插件化 R4c）」⇒ **乙片段**：vr_guard 与 countermeasure owner 均已删除（vr_guard (a) 4a182217）；
  - L30「段名 → backend.cve_2026_43499.*（含 route.*/execution.*）与 common.*」⇒ **丙片段**：前半（owner-qualified 段名）**仍现行**（契约 §3.20），后半 common.* 已废；
  - L38「NativeProfileDocument 选区感知：own 段 + common.*」⇒ **丙片段**：选区感知仍现行，common.* 已废。
- **生产路径判据（实测）**：contract/countermeasure.hpp **仍在生产路径被 include**（contract/step_plan.hpp:24-26）⇒ 按 §九 例外 2 判**非过期**；common.* / countermeasure.* owner 无生产写入者（schema.hpp:114 注明 vivo consumer 已删；profile_v3_test.cpp:199/206 断言出现即拒）。
- **未归档原因**：该件后半（owner-qualified 段名与选区感知）**仍是现行依据**；整篇归档会丢掉仍有效的段名映射结论。
- **待裁提示**：若 Lead 的政策是「**已实行的 implementation 记录**（s4-r2/r3/r6 系列）一律归档」，本件应改判为归档 ⇒ 请裁（本窗口按「丙 ⇒ 留住」执行）。
