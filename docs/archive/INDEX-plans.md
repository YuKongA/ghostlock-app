# 归档索引 · 计划类（INDEX-plans）

> 本文件是 `docs/archive/README.md` 的子索引（≤8 KiB）：批次 1/2/3 计划归档记录 + **`docs/plan` 批次（28 件）** + **UML 归档（1 件，2026-10-08）**。命名/规则见 README；证据索引见 `INDEX-device-gates.md`。
> **压缩声明**：§二/§三/§五 散文已压缩为表 + 结论（决策/日期/路径逐条保留）；原文见 git 历史。

## 一、批次 1 归档索引（已实行的计划，**2 件**）

| 原路径 | 归档路径 | 归档原因 | 日期 |
|---|---|---|---|
| docs/plan/gradle-test-decoupling-plan.md | docs/archive/20261006-1809-gradle-test-decoupling-plan.md | **已实行**（done=5 / open=0；产物：app/build.gradle.kts 的 generateBuildInfo 与 merge*JniLibFolders 挂接） | 2026-10-06 |
| docs/plan/host-attack-dataflow-plan.md | docs/archive/20261006-1809-host-attack-dataflow-plan.md | **已实行**（done=7 / open=0；产物：core/Makefile 的 host-attack-dataflow-test 目标 + core/src/tests/host/host_attack_script.hpp） | 2026-10-06 |


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

- **`- [ ]` 只在【任务清单】语境表示「未完成」**；在【约束 / 规范 / 检查表 / 指南】语境**不表示未执行** ⇒ **按内容性质归类**（例：`docs/analysis/ancillary-controller-guide.md` 的 `- [ ]` 是编码约束 ⇒ 判「现行」留住）。



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

**引用更新（本批）**：AGENTS.md(4) · requirements.md(4) · engineering-standards.md(3) · engineering-rules.md(1) · full-process-uml.md(1) · pr-note-vr-guard-pr.md（1）· docs/analysis/**（13 份 40 处）· src README（1）。

**悬空引用（实测更正）**：源内 **10 处**注释原指向**从未存在**的 `docs/analysis/<plan>.md`（countermeasure-plugin ×3 · minimal-lkm ×1 · 43284-refactor ×4 · branch-plan ×2，file:line 见 git 历史）。**本批已全部修正**（见 `docs/plan/MASTER-PLAN.md` §7 R-8）：现状 = **0**，16 处改 archive 路径；仅 `libextract.so` 二进制串残留（不作判据）。

## 七、2026-10-17 批次：Lua / GLKv3 文档 / CLI / script 面删除（**30 件**）

> 裁定见 `docs/plan/MASTER-PLAN.md` §0.1：vendored lua/mpack/nlohmann 删除、JNI 不可达的旧代码删除；下列设计与工作包随之归档（历史不删）。

| 原路径 | 归档路径 | 归档原因 | 日期 |
|---|---|---|---|
| `docs/analysis/profile-lua-bridge-plan.md` | `docs/archive/20261017-1835-profile-lua-bridge-plan.md` | **Lua 中转载体删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/lua-order-design.md` | `docs/archive/20261017-1835-lua-order-design.md` | **Lua 执行顺序载体删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/script-layer-precedents.md` | `docs/archive/20261017-1835-script-layer-precedents.md` | **脚本层删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/lua-surface-design.md` | `docs/archive/20261017-1835-lua-surface-design.md` | **Lua 暴露面删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/handoff-plugin-lua.md` | `docs/archive/20261017-1835-handoff-plugin-lua.md` | **Lua 插件控制面删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/script-plan-design.md` | `docs/archive/20261017-1835-script-plan-design.md` | **计划脚本删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/handoff-script-layer-p5-spec.md` | `docs/archive/20261017-1835-handoff-script-layer-p5-spec.md` | **P5 脚本层工作包作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/lua-batch-slice-plan.md` | `docs/archive/20261017-1835-lua-batch-slice-plan.md` | **Lua 批次切页作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/lua-config-structure-options.md` | `docs/archive/20261017-1835-lua-config-structure-options.md` | **Lua 配置结构选定作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/profile-pipeline-flow.md` | `docs/archive/20261017-1835-profile-pipeline-flow.md` | **wire/profile 流水线删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/terminal-steps-redesign.md` | `docs/archive/20261017-1835-terminal-steps-redesign.md` | **terminal/pipeline 删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/43499-nlohmann-swap-acceptance.md` | `docs/archive/20261017-1835-43499-nlohmann-swap-acceptance.md` | **nlohmann 已删除，换库验收作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/config-wire-redesign-plan.md` | `docs/archive/20261017-1835-config-wire-redesign-plan.md` | **config/wire 重设计作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/resolved-profile-registry-plan.md` | `docs/archive/20261017-1835-resolved-profile-registry-plan.md` | **profile 注册套件作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/composition-layer-p4-spec.md` | `docs/archive/20261017-1835-composition-layer-p4-spec.md` | **composition_root 删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/wire-transport-model.md` | `docs/archive/20261017-1835-wire-transport-model.md` | **GLKv3 wire（mpack）删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/script-plan-migration.md` | `docs/archive/20261017-1835-script-plan-migration.md` | **脚本计划迁移作废**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/43499-plan-lua-log-verb.md` | `docs/archive/20261017-1835-43499-plan-lua-log-verb.md` | **Lua 日志面删除**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/access-capability-layering.md` | `docs/archive/20261017-1835-access-capability-layering.md` | **访问能力层从未接线，已撤销**（2026-10-17 裁定，见 MASTER-PLAN §0.1） | 2026-10-17 |
| `docs/analysis/adr/0008-lua-runtime-sandbox.md` | `docs/archive/20261017-1835-adr-0008-lua-runtime-sandbox.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/analysis/adr/0009-script-trust-and-hardline.md` | `docs/archive/20261017-1835-adr-0009-script-trust-and-hardline.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/analysis/adr/0010-lua-cpp-resource-model.md` | `docs/archive/20261017-1835-adr-0010-lua-cpp-resource-model.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/analysis/adr/0011-lua-execution-order-authority.md` | `docs/archive/20261017-1835-adr-0011-lua-execution-order-authority.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/analysis/adr/0015-profile-through-lua.md` | `docs/archive/20261017-1835-adr-0015-profile-through-lua.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/analysis/adr/0016-script-argument-crossing.md` | `docs/archive/20261017-1835-adr-0016-script-argument-crossing.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/analysis/adr/0017-jcs-canonical-form.md` | `docs/archive/20261017-1835-adr-0017-jcs-canonical-form.md` | **ADR 废除**（Lua / script / JCS 载体删除） | 2026-10-17 |
| `docs/plan/rewrite-plan.md` | `docs/archive/20261017-1835-rewrite-plan.md` | **工作包作废**（Lua / GLKv3 文档 / CLI / script 面删除） | 2026-10-17 |
| `docs/plan/rewrite-plan-v2.md` | `docs/archive/20261017-1835-rewrite-plan-v2.md` | **工作包作废**（Lua / GLKv3 文档 / CLI / script 面删除） | 2026-10-17 |
| `docs/plan/B0.0-REVISION.md` | `docs/archive/20261017-1835-B0.0-REVISION.md` | **工作包作废**（Lua / GLKv3 文档 / CLI / script 面删除） | 2026-10-17 |
| `docs/plan/A6-baseline-approval.md` | `docs/archive/20261017-1835-A6-baseline-approval.md` | **工作包作废**（Lua / GLKv3 文档 / CLI / script 面删除） | 2026-10-17 |

## 九、2026-10-21 批次：43284 旧计划合并（**2 件**）

> 依据：43284 于 2026-10-19 重置（设计与批次见 `docs/analysis/43284-refactor-design-v4.md` + `docs/plan/43284-reset-batch.md`）。
> 被取代的两件归档；**仍在办的 v3、被引用的 hook/production-engine 计划原地保留**（清单见 `43284-reset-batch.md` §旧计划归档与合并）。

| 原路径 | 归档路径 | 归档原因 | 日期 |
|---|---|---|---|
| `docs/plan/43284-b8-b9-plan-v2.md` | `docs/archive/20261019-2309-43284-b8-b9-plan-v2.md` | **被 v3 取代**；删除结论已并入 v3 与 B5 | 2026-10-21 |
| `docs/plan/43284-b8-b9-b10-plan.md` | `docs/archive/20261019-2309-43284-b8-b9-b10-plan.md` | **被 v3 取代**；Lua 编排段已被 §0.1 废除 | 2026-10-21 |

- 存档时间取「最后修改提交」（两者同为 `0ed4e6d6`，2026-10-19 23:09:19 -0400）。
- 同批不归档但改动头部指针的：`43284-b8-b9-b10-plan-v3.md`、`43284-kotlin-dynamic-hook-plan.md`、`43284-production-engine-plan.md`、`43284-reset-batch.md`。

## 八、UML 归档（**1 件**，2026-10-08）

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| `docs/development/full-process-uml.md` | `docs/development/old-full-process-uml.md` | **旧实现内容未随 A0–A6 更新** | 2026-10-08 |

## 2026-10-29 计划压缩归档

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| docs/plan/exec-glue-plan.md | docs/archive/20261029-1025-exec-glue-plan-history.md | rev1-rev4 详版历史保留；唯一活动计划压缩至 A3 配额 | 2026-10-29 |

## 十、2026-11-04 Backend root execution 需求重建（**4 件**）

> 用户裁定旧 KBR 把 Payload/Finish 错误设计成 runtime executor，中心控制流失效。旧 KBR 与其已取代的 KC 稿一起归档；新的唯一需求入口为 `docs/plan/backend-root-execution-unification-plan.md`。

| 原路径 | 归档路径 | 归档原因 | 日期 |
|---|---|---|---|
| `docs/plan/kotlin-core-backend-vertical-rewrite-plan.md` | `docs/archive/20261104-1935-kotlin-core-backend-vertical-rewrite-plan.md` | **用户裁定失效**：错误引入 Payload/Finish/PostRoot executor，未按单一落盘 root 脚本建模 | 2026-11-04 |
| `docs/plan/kotlin-core-backend-vertical-rewrite-annex-a.md` | `docs/archive/20261104-1935-kotlin-core-backend-vertical-rewrite-annex-a.md` | **主计划失效**：旧 exact file ledger 不再授权实施 | 2026-11-04 |
| `docs/plan/kotlin-core-observability-cleanup-plan.md` | `docs/archive/20261104-1935-kotlin-core-observability-cleanup-plan.md` | **被后继需求吸收**：compatibility-first KC 路线不再是活动计划 | 2026-11-04 |
| `docs/plan/kotlin-core-observability-cleanup-annex-a.md` | `docs/archive/20261104-1935-kotlin-core-observability-cleanup-annex-a.md` | **所属计划被取代**：旧 wire/parity/执行账本只保留沿革 | 2026-11-04 |

## 十一、2026-11-04 Backend root execution authority 收口（**72 件**）

> 用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1。下列 72 件旧活动计划按 exact disposition 归档；正文保留历史，现行唯一后继为 `docs/plan/backend-root-execution-unification-plan.md`。
> 2026-11-05 后继计划推进至 R4 后，用户纠正 automatic export 是产品能力而非删除项，并批准R4A source重建；归档正文仍只保留历史，不恢复implementation authority。R4A source已落地，R2-R4A gate仍保持NOT_RUN。

| 原路径 | 归档路径 | 原因 | 后继 |
|---|---|---|---|
| `docs/plan/43284-b8-b9-b10-plan-v3.md` | `docs/archive/20261104-2030-43284-b8-b9-b10-plan-v3.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43284-kotlin-dynamic-hook-plan.md` | `docs/archive/20261104-2030-43284-kotlin-dynamic-hook-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43284-production-engine-plan.md` | `docs/archive/20261104-2030-43284-production-engine-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43284-reset-batch.md` | `docs/archive/20261104-2030-43284-reset-batch.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-43284-cpp-parameterization-plan.md` | `docs/archive/20261104-2030-43499-43284-cpp-parameterization-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-43284-jni-rewrite-plan.md` | `docs/archive/20261104-2030-43499-43284-jni-rewrite-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-5.10-attack-path-adaptation-plan.md` | `docs/archive/20261104-2030-43499-5.10-attack-path-adaptation-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-agent-coding-and-self-acceptance-guide.md` | `docs/archive/20261104-2030-43499-agent-coding-and-self-acceptance-guide.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-app-domain-execution-model-plan-annex.md` | `docs/archive/20261104-2030-43499-app-domain-execution-model-plan-annex.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-app-domain-execution-model-plan.md` | `docs/archive/20261104-2030-43499-app-domain-execution-model-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-approval-status.md` | `docs/archive/20261104-2030-43499-approval-status.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-c11-header-contract-plan.md` | `docs/archive/20261104-2030-43499-c11-header-contract-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-fast-repair-prebuilt-plan-annex.md` | `docs/archive/20261104-2030-43499-fast-repair-prebuilt-plan-annex.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-fast-repair-prebuilt-plan.md` | `docs/archive/20261104-2030-43499-fast-repair-prebuilt-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-jni-transport-plan.md` | `docs/archive/20261104-2030-43499-jni-transport-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-kotlin-package-refactor-plan.md` | `docs/archive/20261104-2030-43499-kotlin-package-refactor-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-kt-step-plan.md` | `docs/archive/20261104-2030-43499-kt-step-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-ktstep-architecture-plan.md` | `docs/archive/20261104-2030-43499-ktstep-architecture-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-legacy-cpp-jni-migration-guide.md` | `docs/archive/20261104-2030-43499-legacy-cpp-jni-migration-guide.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-legacy-cpp-production-inventory.md` | `docs/archive/20261104-2030-43499-legacy-cpp-production-inventory.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-legacy-cpp-test-inventory.md` | `docs/archive/20261104-2030-43499-legacy-cpp-test-inventory.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-logging-integration-plan.md` | `docs/archive/20261104-2030-43499-logging-integration-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-native-actor-child-plan.md` | `docs/archive/20261104-2030-43499-native-actor-child-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-native-device-ops-plan.md` | `docs/archive/20261104-2030-43499-native-device-ops-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-native-parameterization-v2-plan.md` | `docs/archive/20261104-2030-43499-native-parameterization-v2-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-perf-leak-primitive-plan-annex-b.md` | `docs/archive/20261104-2030-43499-perf-leak-primitive-plan-annex-b.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-perf-leak-primitive-plan-annex.md` | `docs/archive/20261104-2030-43499-perf-leak-primitive-plan-annex.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-perf-leak-primitive-plan.md` | `docs/archive/20261104-2030-43499-perf-leak-primitive-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-prepare-port-plan.md` | `docs/archive/20261104-2030-43499-prepare-port-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-production-engine-plan.md` | `docs/archive/20261104-2030-43499-production-engine-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-production-victim-plan.md` | `docs/archive/20261104-2030-43499-production-victim-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-public-structure-adaptation-plan.md` | `docs/archive/20261104-2030-43499-public-structure-adaptation-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-r1-freeze-abi.md` | `docs/archive/20261104-2030-43499-r1-freeze-abi.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-r1-freeze-fields.md` | `docs/archive/20261104-2030-43499-r1-freeze-fields.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-r1-freeze-inventory.md` | `docs/archive/20261104-2030-43499-r1-freeze-inventory.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-r1-freeze-validation.md` | `docs/archive/20261104-2030-43499-r1-freeze-validation.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-resource-primitives-implementation-plan.md` | `docs/archive/20261104-2030-43499-resource-primitives-implementation-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-route-port-plan.md` | `docs/archive/20261104-2030-43499-route-port-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-tcp-native-port-plan.md` | `docs/archive/20261104-2030-43499-tcp-native-port-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-three-routes-production-plan.md` | `docs/archive/20261104-2030-43499-three-routes-production-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-victim-launch-hoist-adaptation-plan.md` | `docs/archive/20261104-2030-43499-victim-launch-hoist-adaptation-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/43499-w3-hard-gate-plan.md` | `docs/archive/20261104-2030-43499-w3-hard-gate-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-43499-conf-reuse-classification.md` | `docs/archive/20261104-2030-98260-43499-conf-reuse-classification.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-backend-plan-annex-b.md` | `docs/archive/20261104-2030-98260-backend-plan-annex-b.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-backend-plan-annex.md` | `docs/archive/20261104-2030-98260-backend-plan-annex.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-backend-plan-delta-userservice.md` | `docs/archive/20261104-2030-98260-backend-plan-delta-userservice.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-backend-plan.md` | `docs/archive/20261104-2030-98260-backend-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-project-structure-adaptation-plan.md` | `docs/archive/20261104-2030-98260-project-structure-adaptation-plan.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/98260-user-requirements-summary.md` | `docs/archive/20261104-2030-98260-user-requirements-summary.md` | old backend/JNI/execution authority; preserve as history | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/PENDING-DECISIONS.md` | `docs/archive/20261104-2030-PENDING-DECISIONS.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/backend-access-execution-design.md` | `docs/archive/20261104-2030-backend-access-execution-design.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/backend-directory-cleanup.md` | `docs/archive/20261104-2030-backend-directory-cleanup.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/backend-registry-unification.md` | `docs/archive/20261104-2030-backend-registry-unification.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/backends-execution-architecture-plan.md` | `docs/archive/20261104-2030-backends-execution-architecture-plan.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/cmake-backend-chunks.md` | `docs/archive/20261104-2030-cmake-backend-chunks.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/debug-probe-backend.md` | `docs/archive/20261104-2030-debug-probe-backend.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/exec-glue-plan.md` | `docs/archive/20261104-2030-exec-glue-plan.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/execution-architecture.md` | `docs/archive/20261104-2030-execution-architecture.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/flow-process-launcher-annex-b4-real.md` | `docs/archive/20261104-2030-flow-process-launcher-annex-b4-real.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/flow-process-launcher-annex-b4a.md` | `docs/archive/20261104-2030-flow-process-launcher-annex-b4a.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/flow-process-launcher-annex-b4b.md` | `docs/archive/20261104-2030-flow-process-launcher-annex-b4b.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/flow-process-launcher-annex-b5-closeout.md` | `docs/archive/20261104-2030-flow-process-launcher-annex-b5-closeout.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/flow-process-launcher-annex-config-document-port.md` | `docs/archive/20261104-2030-flow-process-launcher-annex-config-document-port.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/flow-process-launcher-plan.md` | `docs/archive/20261104-2030-flow-process-launcher-plan.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/kotlin-clean-architecture-ui-plan-annex-a.md` | `docs/archive/20261104-2030-kotlin-clean-architecture-ui-plan-annex-a.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/kotlin-clean-architecture-ui-plan-annex-b.md` | `docs/archive/20261104-2030-kotlin-clean-architecture-ui-plan-annex-b.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/kotlin-clean-architecture-ui-plan-v0-checklist.md` | `docs/archive/20261104-2030-kotlin-clean-architecture-ui-plan-v0-checklist.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/kotlin-clean-architecture-ui-plan.md` | `docs/archive/20261104-2030-kotlin-clean-architecture-ui-plan.md` | completed/current-fact plan superseded by root execution authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/payload-finish-chain-plan.md` | `docs/archive/20261104-2030-payload-finish-chain-plan.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/rewrite-disable-manifest.tsv` | `docs/archive/20261104-2030-rewrite-disable-manifest.tsv` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/split-large-functions.md` | `docs/archive/20261104-2030-split-large-functions.md` | completed original-backend implementation plan superseded by R1 deletion closure | `docs/plan/backend-root-execution-unification-plan.md` |
| `docs/plan/unified-execution-engine-plan.md` | `docs/archive/20261104-2030-unified-execution-engine-plan.md` | superseded overlapping execution/config/rollback authority | `docs/plan/backend-root-execution-unification-plan.md` |
