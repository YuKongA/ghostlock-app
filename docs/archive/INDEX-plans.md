# 归档索引 · 计划类（INDEX-plans）

> 本文件是 `docs/archive/README.md` 的子索引（≤8 KiB）：批次 1/2/3 计划归档记录 + **`docs/plan` 批次（28 件，2026-10-07 22:37）**。总览/命名/规则见 README；恢复与证据索引见 `INDEX-device-gates.md`。
> **压缩声明**：§二/§三/§五 的散文已压缩为表 + 结论（决策、日期、路径逐条保留）；原文见 git 历史。

## 一、批次 1 归档索引（已实行的计划，**2 件**）

| 原路径 | 归档路径 | 归档原因 | 日期 |
|---|---|---|---|
| docs/plan/gradle-test-decoupling-plan.md | docs/archive/20261006-1809-gradle-test-decoupling-plan.md | **已实行**（done=5 / open=0；产物：app/build.gradle.kts 的 generateBuildInfo 与 merge*JniLibFolders 挂接） | 2026-10-06 |
| docs/plan/host-attack-dataflow-plan.md | docs/archive/20261006-1809-host-attack-dataflow-plan.md | **已实行**（done=7 / open=0；产物：src/Makefile 的 host-attack-dataflow-test 目标 + src/core/tests/host/host_attack_script.hpp） | 2026-10-06 |


## 二、批次 2 评估记录（**0 件归档**）

本轮审 3 件，**全判丙（混合）⇒ 留住**（未移动文件）：

| 文件 | 判定 | 理由 |
|---|---|---|
| `a2-4-5-plan.md` | 丙 ⇒ 留住 | 目标态部分已实现、部分被撤销（vivo 段） |
| `kernel-memory-batch1-plan.md` | 丙 ⇒ 留住 | contract/countermeasure.hpp 生产在用；platform/vivo 段已删 |
| `config-wire-redesign-plan.md` | 丙 ⇒ 留住 | R8 组织原则仍现行；vivo/countermeasure 迁移段作废 |

**判据（本轮确立）**：先用 `grep` 判「生产路径是否仍使用」，**再**读正文判甲/乙/丙；**命中数不是过期信号**。
（原文 4 条「非过期例外」的标识符因反引号事故丢失；结论并入上行判据。）

## 三、批次 3 评估与归档政策

| 文件 | 判定 | 动作 | 理由 |
|---|---|---|---|
| `docs/analysis/s4-r2-implementation.md` | **已实行 ⇒ 归档**（原判「丙 ⇒ 留住」被取代） | ⇒ `docs/archive/20261005-1219-s4-r2-implementation.md` | implementation 记录（政策 1）；后半由契约 3.20 承载 |

**本批归档 1 件**（analysis 29 → 28；archive 55 → 56）。

### 归档政策（2026-10-06 Lead 裁决，**优先于「丙 ⇒ 留住」**）

1. **implementation 记录**（s4-r2-… / s4-r3-… / s4-r6-… 一类「某子步骤已落地」的日志）⇒ **判「已实行」⇒ 归档**；
2. **例外（不归档）**：① 仍有 **open 项**；② **被在途工作引用**；③ **仍是现行契约/机制权威**（如 contract-design.md、wire-transport-model.md、step-* 现役契约）；
3. **丙（混合）** 按 §二 处理（留住 + 标注），**但若整件本质是「已实行的记录」⇒ 按第 1 条归档**（**本条优先**）。

**待 Lead 裁**：是否把「已实行的 implementation 记录」（s4-r2/r3/r6 系列）统一归档。

## 四、批次 3 收口（1 件归档）

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| docs/plan/vr-guard-plan.md | docs/archive/20261006-1809-vr-guard-plan.md | **已实行**（done/open=12/0；点名提交均存在；生产路径仅剩删除说明注释） | 2026-10-06 |


### 判据固化（2026-10-06）：`- [ ]` 的语境

- **`- [ ]` 只在【任务清单】语境下表示「未完成」**；在【约束 / 规范 / 检查表 / 指南】语境下**不表示未执行** ⇒ **按其内容性质归类**（例：`docs/analysis/ancillary-controller-guide.md` 的 `- [ ]` 是编码约束 ⇒ 判「现行」留住）。



## 五、批次 3 第 2 件（1 件归档）

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| `docs/analysis/43284-logging-and-plugin-log-design.md` | `docs/archive/20261006-1809-43284-logging-and-plugin-log-design.md` | **设计已落地**（diag_line.hpp 与 GLK_CAP_LOG 在生产路径） | 2026-10-06 |

### 政策补充（2026-10-06，Lead 裁决）：【待落地项】≠【过时片段】

- **计划里「将新增/将创建」且全仓 find 无同类的名字 ⇒ 判【待落地项】** ⇒ 属**未执行**，本件仍现行 ⇒ **留作路线图，不标注**；
- **曾有、现已改名/并入/删除的名字 ⇒ 判【过时片段】** ⇒ 按「丙」追加「归档评估」节（不改正文）；
- 三类事实须**分别记录**：**已迁移** / **已改名** / **已删除**；不得笼统写作「过时片段」。

## 六、`docs/plan` 批次归档（**28 件**，2026-10-07 22:37）

**原因**：计划收敛为**一份主计划** `docs/plan/MASTER-PLAN.md`（守则 §2 生命周期 + §5 策划输入/输出）；其余计划未按现行设计规范编写 ⇒ 全部归档保留沿革。

**规则**：每件头部含「原始路径 / 归档原因 / 归档日期 / 来源」；正文逐字节未改；全仓 `docs/plan/<name>.md` 引用 ⇒ archive 路径，现行指针 ⇒ `MASTER-PLAN.md`。

| 归档文件名（前缀 `docs/archive/20261007-2237-`） | 原路径 |
|---|---|
| `43284-gate-m5-selection-plan.md` | `docs/plan/43284-gate-m5-selection-plan.md` |
| `5x-tcp-geometry-plan.md` | `docs/plan/5x-tcp-geometry-plan.md` |
| `MASTER-PLAN.md` | `docs/plan/MASTER-PLAN.md` |
| `a2-4-5-plan.md` | `docs/plan/a2-4-5-plan.md` |
| `a3-kernelsnitch-plan.md` | `docs/plan/a3-kernelsnitch-plan.md` |
| `ancillary-vr-task-tag-migration-plan.md` | `docs/plan/ancillary-vr-task-tag-migration-plan.md` |
| `available-handoff-plan.md` | `docs/plan/available-handoff-plan.md` |
| `branch-plan.md` | `docs/plan/branch-plan.md` |
| `config-wire-redesign-plan.md` | `docs/plan/config-wire-redesign-plan.md` |
| `contract-first-chain-plan.md` | `docs/plan/contract-first-chain-plan.md` |
| `countermeasure-plugin-plan.md` | `docs/plan/countermeasure-plugin-plan.md` |
| `cve-2026-43284-backend-plan.md` | `docs/plan/cve-2026-43284-backend-plan.md` |
| `cve-2026-43284-refactor-plan.md` | `docs/plan/cve-2026-43284-refactor-plan.md` |
| `exploit-session-generalization-plan.md` | `docs/plan/exploit-session-generalization-plan.md` |
| `extract-5x-route-suggestion-plan.md` | `docs/plan/extract-5x-route-suggestion-plan.md` |
| `extract-pselect-fallback-plan.md` | `docs/plan/extract-pselect-fallback-plan.md` |
| `extractor-no-btf-struct-derivation-plan.md` | `docs/plan/extractor-no-btf-struct-derivation-plan.md` |
| `flexible-kernel-rw-primitive-plan.md` | `docs/plan/flexible-kernel-rw-primitive-plan.md` |
| `host-assembled-integration-test-plan.md` | `docs/plan/host-assembled-integration-test-plan.md` |
| `kernel-memory-batch1-plan.md` | `docs/plan/kernel-memory-batch1-plan.md` |
| `kernel-phys-offset-plan.md` | `docs/plan/kernel-phys-offset-plan.md` |
| `minimal-lkm-plan.md` | `docs/plan/minimal-lkm-plan.md` |
| `offset-ssot-plan.md` | `docs/plan/offset-ssot-plan.md` |
| `plugin-system-future-plan.md` | `docs/plan/plugin-system-future-plan.md` |
| `profile-editor-complete-fields-plan.md` | `docs/plan/profile-editor-complete-fields-plan.md` |
| `software-engineering-plan.md` | `docs/plan/software-engineering-plan.md` |
| `tiered-custom-handoff-unfreeze-plan.md` | `docs/plan/tiered-custom-handoff-unfreeze-plan.md` |
| `top-level-architecture-rewrite-plan.md` | `docs/plan/top-level-architecture-rewrite-plan.md` |

**引用更新（本批）**：AGENTS.md（4）· requirements.md（4）· engineering-standards.md（3）· engineering-rules.md（1）· full-process-uml.md（1）· pr-note-vr-guard-pr.md（1）· docs/analysis/**（13 份 40 处）· src README（1）。

**悬空引用（M3 实测更正）**：审查时源内实测 **10 处**代码注释指向**从未存在**的 `docs/analysis/<plan>.md`（countermeasure-plugin ×3：glk_contract_abi.h:12 / loader.hpp:6 / registry.hpp:6；minimal-lkm ×1：lkm_kmi_manifest_test.cpp:4；43284-refactor ×4：pagecache.hpp:20 / splice_io.hpp:16 / ipsec.hpp:7 / SessionSecretFrame.kt:62；branch-plan ×2：main.rs:73/:206）。**本批（task-65/66）已全部修正**：现状指向 `docs/analysis/<plan>.md` 的引用 = **0**，16 处改为 archive 路径；仅 `libextract.so` 二进制字符串残留（不作判据）。
