# 归档说明（docs/archive）

> 本目录存放**已实现完的计划**、**不再需要的分析**与**一次性交接稿**（用户指令 2026-10-06：把 `analysis` 拆成 `analysis` 与 `plan`，并新建归档目录）。

## 一、目录分工（可复核判据）

| 目录 | 放什么 | 判据 |
|---|---|---|
| `docs/analysis/` | **现状 / 机制 / 契约 / 设计参考**（描述「是什么」） | 当前仍有效的契约、格式、机制说明；**在途工作所依据的设计稿**（例 `execution-combination-menu-and-general-profile.md`） |
| `docs/plan/` | **未来要做什么**（描述「要做什么」） | 文件名以 `-plan.md` 结尾，或内容为「计划 / 待做 / 未来形态 / 冻结待解冻」且**条目尚未落地**；**单一进度入口 `branch-plan.md` 必在此** |
| `docs/archive/`（本目录） | **已实现完的 plan** / **不再需要的 analysis** / **一次性交接稿** | ① 其验收项已勾选或对应提交已落地；② 所描述的机制已不存在或被取代；③ 内容已被后续文档吸收 |

**就地保留（不进本目录）**：
- `docs/development/**`：工程规范（长期有效）；
- `docs/analysis/adr/**`：决策序列（ADR 是历史记录形态，不迁移）；
- **`docs/analysis/device-gates/**`：门禁证据 —— 本身即归档形态，故就地保留、不迁入本目录**（迁移会破坏「证据在某提交下原样存在」的可追溯性，且门禁记录里的原始日志文本不应被改名/搬动）。

## 二、命名规则

- 归档文件名 = **`YYYYMMDD-HHMM-<原文件名>`**（可排序、无歧义）；
- **时间取值**：① **已实现 / 不再需要**的 ⇒ 取该文件**最后一次修改提交的时间**（`git log -1 --format=%ci -- <path>`）；② **从 git 历史恢复到本目录**的 ⇒ 取**删除它的那个提交的时间**，并在文件头部加**恢复说明**（原始路径 / 删除提交 / 恢复来源 `git show <commit>^:<path>` / 恢复日期）；
- 索引表见下（新归档一律**追加一行**）。
**前缀幂等（防再犯，硬性自检）**：归档命名必须**幂等**——
- 加前缀前先判断：**若文件名已匹配 `^[0-9]{8}-[0-9]{4}-`，则不再追加前缀**（直接使用原文件名）；
- 脚本化的自检断言：`assert(!/^[0-9]{8}-[0-9]{4}-/.test(originalName) || newName === originalName)`；批量改名后必须跑一次**全目录扫描**：`ls docs/archive/ | grep -E "^[0-9]{8}-[0-9]{4}-[0-9]{8}-[0-9]{4}-"` ⇒ **必须为空**；
- 前缀只有一个，且**与内容来源一致**：**已实现/不再需要**的文件取「最后修改提交时间」；**从 git 恢复**的文件取「删除它的提交时间」（并在文件头写明恢复来源）。⇒ **两者混用时以「内容来源」为准**，不做二次追加。
- **本项目真实事故（记录以免重演）**：一次性批量替换脚本里同时做了「整路径替换」与「basename 替换」，导致前缀被应用两次（`20261006-1712-20261006-1712-step-queue-design.md`）；**修正** = 保留**与内容来源一致**的那一个前缀（step-queue-design 取**最后修改**时间 20261006-1712），全目录扫描确认 **0** 个双前缀，引用同步后 **0 悬空**。

## 三、索引（原路径 → 归档路径 → 日期/依据）

| 原路径 | 归档路径 | 日期（取自提交） | 归档依据 |
|---|---|---|---|
| `docs/analysis/step-queue-design.md` | `docs/archive/20261006-1712-step-queue-design.md` | 2026-10-06 17:12:33 -0400 | **已实现完**：M3（`d34caa99`+`6a3d60c6`）与 M5（`e59a8479`+归档 `af2feefc`）均已落地并在 `branch-plan` 标「已完成」 |
| `docs/analysis/fallback-route-wire-plan.md` | `docs/archive/20261006-1712-fallback-route-wire-plan.md` | 2026-10-06 17:12:33 -0400 | **机制已不存在**：旧 fallback 派发在 R6a 被移除（`ProfileResolver.kt:45` 注释），其配置语法属 v2 legacy |
| `docs/analysis/m5-handoff-20261006.md` | `docs/archive/20261006-1426-m5-handoff-20261006.md` | 2026-10-06 14:26:43 -0400 | **一次性交接稿**：内容已被 M5 收口文档与 `branch-plan` 吸收 |

## 四、维护约定

- **移入本目录前先查引用**：若某文件正被在途工作引用（他人照其 §x 执行），**原地保留**并在批次报告里列出「因被引用而保留」清单；确需移动时，**同批更新全部引用**并 grep 复验无悬空路径；
- **不迁移证据**：`docs/analysis/device-gates/**` 见上；
- 归档**不等于删除**：历史一律保留在 git 中，本目录只是「不再作为当前依据」的显式标志。

## 五、本次恢复索引（删除提交 -> 归档路径）

| 原路径 | 归档路径 | 删除提交 |
|---|---|---|
| docs/analysis/native-component-architecture-plan.md | docs/archive/20260927-0029-native-component-architecture-plan.md | c50e3ef4 |
| docs/analysis/batch4-slice3c-hook-contract.md | docs/archive/20260927-0029-batch4-slice3c-hook-contract.md | c50e3ef4 |
| docs/analysis/batch1-2-review-fixes.md | docs/archive/20260927-0029-batch1-2-review-fixes.md | c50e3ef4 |
| docs/analysis/environment-convergence-plan.md | docs/archive/20260922-2231-environment-convergence-plan.md | 064cd1e5 |
| docs/analysis/wire-v2-kotlin-handoff.md | docs/archive/20260927-0029-wire-v2-kotlin-handoff.md | c50e3ef4 |
| docs/analysis/native-modernization-candidates.md | docs/archive/20260922-2231-native-modernization-candidates.md | 064cd1e5 |
| docs/analysis/multicast-route-class-plan.md | docs/archive/20260922-2231-multicast-route-class-plan.md | 064cd1e5 |
| docs/analysis/ipo-and-state-machines.md | docs/archive/20261005-1624-ipo-and-state-machines.md | c2cc25c1 |
| docs/analysis/wire-v2-object-sections-plan.md | docs/archive/20260927-0029-wire-v2-object-sections-plan.md | c50e3ef4 |
| docs/analysis/remove-multicast-resident-plan.md | docs/archive/20260927-0029-remove-multicast-resident-plan.md | c50e3ef4 |
| docs/analysis/native-global-state.md | docs/archive/20260922-2231-native-global-state.md | 064cd1e5 |
| docs/analysis/native-warning-audit.md | docs/archive/20260922-2231-native-warning-audit.md | 064cd1e5 |
| docs/analysis/latest-debug.log | docs/archive/20260922-2231-latest-debug.log | 064cd1e5 |
| RELEASE_NOTE.md | docs/archive/20260910-1817-RELEASE_NOTE.md | f8faea35 |
| docs/development/native-modernization-plan.md | docs/archive/20260922-2231-native-modernization-plan.md | 994ccf81 |
| docs/analysis/native-functions.md | docs/archive/20260922-2231-native-functions.md | 064cd1e5 |
| docs/development/adding-a-route.md | docs/archive/20260924-2231-adding-a-route.md | 40e6cfc1 |
| docs/analysis/native-component-batch3-design.md | docs/archive/20260927-0029-native-component-batch3-design.md | c50e3ef4 |
| docs/analysis/all-functions-callgraph.md | docs/archive/20260922-2231-all-functions-callgraph.md | 064cd1e5 |
| docs/analysis/extractor-hocon-output-plan.md | docs/archive/20260927-0029-extractor-hocon-output-plan.md | c50e3ef4 |
| docs/analysis/extractor-5x-510-layout-plan.md | docs/archive/20260927-0029-extractor-5x-510-layout-plan.md | c50e3ef4 |
| docs/analysis/unverified-profile-candidate-plan.md | docs/archive/20260927-0029-unverified-profile-candidate-plan.md | c50e3ef4 |
| docs/analysis/native-exit-path-audit.md | docs/archive/20260922-2231-native-exit-path-audit.md | 064cd1e5 |
| docs/analysis/native-entrypoint-plan.md | docs/archive/20260922-2231-native-entrypoint-plan.md | 064cd1e5 |
| docs/analysis/native-component-batch1-design.md | docs/archive/20260927-0029-native-component-batch1-design.md | c50e3ef4 |
| docs/analysis/profile-entry-decoupling.md | docs/archive/20260922-2231-profile-entry-decoupling.md | 064cd1e5 |
| docs/release-note-v1.2.md | docs/archive/20260922-2231-release-note-v1.2.md | ccea55c1 |
| docs/analysis/batch4-pipeline-landing-plan.md | docs/archive/20260927-0029-batch4-pipeline-landing-plan.md | c50e3ef4 |
| docs/analysis/native-component-batch4-design.md | docs/archive/20260927-0029-native-component-batch4-design.md | c50e3ef4 |
| docs/analysis/run-state-tracking-plan.md | docs/archive/20260927-0029-run-state-tracking-plan.md | c50e3ef4 |
| docs/pr-note-very-not-stable-dev.md | docs/archive/20260922-2231-pr-note-very-not-stable-dev.md | 064cd1e5 |
| docs/analysis/native-file-reorg-plan.md | docs/archive/20260922-2231-native-file-reorg-plan.md | 064cd1e5 |
| docs/analysis/known-bugs-2026-09-26-multicast.md | docs/archive/20260927-0029-known-bugs-2026-09-26-multicast.md | c50e3ef4 |
| docs/analysis/native-decoupling-plan.md | docs/archive/20260922-2231-native-decoupling-plan.md | 064cd1e5 |
| docs/analysis/vivo-vr-ko-bypass-plan.md | docs/archive/20260929-0041-vivo-vr-ko-bypass-plan.md | be8b6c44 |
| docs/analysis/native-component-batch2-design.md | docs/archive/20260927-0029-native-component-batch2-design.md | c50e3ef4 |
| docs/analysis/race-lifecycle-cancellation-plan.md | docs/archive/20260927-0029-race-lifecycle-cancellation-plan.md | c50e3ef4 |
| docs/analysis/batch4-slice3-plan.md | docs/archive/20260927-0029-batch4-slice3-plan.md | c50e3ef4 |
| docs/analysis/routes.md | docs/archive/20260922-2231-routes.md | 064cd1e5 |
| docs/analysis/native-cpp-migration-plan.md | docs/archive/20260922-2231-native-cpp-migration-plan.md | 064cd1e5 |
| docs/analysis/initial_research.md | docs/archive/20260927-0029-initial_research.md | c50e3ef4 |
| docs/analysis/batch5-backend-extension-plan.md | docs/archive/20260927-0029-batch5-backend-extension-plan.md | c50e3ef4 |
| docs/analysis/pi-timeout-binary-diff.md | docs/archive/20260922-2231-pi-timeout-binary-diff.md | 064cd1e5 |
| docs/analysis/route-pluggable-config-plan.md | docs/archive/20260922-2231-route-pluggable-config-plan.md | 064cd1e5 |
| docs/analysis/kotlin-native-bridge.md | docs/archive/20260922-2231-kotlin-native-bridge.md | 064cd1e5 |
| docs/analysis/extractor-5x-derivation-plan.md | docs/archive/20260927-0029-extractor-5x-derivation-plan.md | c50e3ef4 |
| docs/analysis/README.md | docs/archive/20260922-2231-README.md | 064cd1e5 |
| docs/analysis/native-cpp-current-uml.md | docs/archive/20260922-2231-native-cpp-current-uml.md | 064cd1e5 |
| docs/analysis/pd2361-5.15.178-manual-geometry.md | docs/archive/20260927-0029-pd2361-5.15.178-manual-geometry.md | c50e3ef4 |
| docs/analysis/upstream-catch-up-20260913.md | docs/archive/20260922-2231-upstream-catch-up-20260913.md | 064cd1e5 |
| docs/analysis/step-queue-design.md | docs/archive/20261006-1712-step-queue-design.md | （随改名归档，非删除） |
| docs/analysis/fallback-route-wire-plan.md | docs/archive/20261006-1712-fallback-route-wire-plan.md | （随改名归档，非删除） |
| docs/analysis/m5-handoff-20261006.md | docs/archive/20261006-1426-m5-handoff-20261006.md | （随改名归档，非删除） |

**恢复计数**：**50 件**（+ 先前随改名归档 3 件 ⇒ 归档目录共 **53 个 .md** +  + 本 README）。

## 六、明确「不恢复」清单与理由（不静默丢弃）

| 类别 | 数量 | 体积 | 不恢复的理由 |
|---|---|---|---|
| docs/analysis/device-gates/**（已删除的历史门禁证据） | **164** | **1445330 B ≈ 1411.5 KiB** | **证据本身就是归档形态**；AGENTS 已指明「需要时从 git 历史取回」；恢复会让仓库显著膨胀。⇒ 需用户/Lead 据此决定是否恢复（本批未恢复） |
| docs/kernel_profiles/templates/**（已删除的旧模板） | **4** | 小 | 已被**改名后的现役** docs/profile/templates/** 取代；恢复会产生两套模板（双真相） |

（其余  与根目录历史文档**已全部恢复**到本目录，见 §五索引。）

## 七、证据类恢复索引（device-gates，**164 件**）

**实测计数**：     164 = **164**（与删除清单中的 device-gates 件数一致）。

- **落点**：（**独立子目录**，不与 50 件文档混层；**未**放回现役的 ）；
- **命名**：（时间 = **删除提交时间**；幂等 ⇒ 已匹配前缀者不再追加）；
- **每件文件头**：原始路径 / 删除提交（sha + 时间）/ 恢复来源  / 恢复日期 ⇒ **映射自描述**，无需另附 164 行表格；
- **按删除提交时间分布（实测）**：
- ：**157** 件
- ：**7** 件

## 八、批次 1 归档索引（已实行的计划，**2 件**）

| 原路径 | 归档路径 | 归档原因 | 日期 |
|---|---|---|---|
| docs/plan/gradle-test-decoupling-plan.md | docs/archive/20261006-1809-gradle-test-decoupling-plan.md | **已实行**（done=5 / open=0；产物：app/build.gradle.kts 的 generateBuildInfo 与 merge*JniLibFolders 挂接） | 2026-10-06 |
| docs/plan/host-attack-dataflow-plan.md | docs/archive/20261006-1809-host-attack-dataflow-plan.md | **已实行**（done=7 / open=0；产物：src/Makefile 的 host-attack-dataflow-test 目标 + src/core/tests/host/host_attack_script.hpp） | 2026-10-06 |

## 九、批次 2 评估记录（**0 件归档**）

本轮审 3 件，**全部判为丙（混合）⇒ 留住**（ 仍 23 件、本目录 .md 仍 55 件，未移动任何文件）：

| 文件 | 判定 | 理由 |
|---|---|---|
| docs/plan/a2-4-5-plan.md | 丙 ⇒ 留住 | 目标态部分已实现（platform 已存在、common.h 已删）、部分已被撤销（vivo 段） |
| docs/plan/kernel-memory-batch1-plan.md | 丙 ⇒ 留住 | contract/countermeasure.hpp 生产在用；platform/vivo 段已删 |
| docs/plan/config-wire-redesign-plan.md | 丙 ⇒ 留住 | R8 组织原则仍现行；vivo/countermeasure 迁移段作废 |

**四条「非过期例外」（命中字符串 ≠ 机制存在，务必按此判）**：

1. ：**冻结保留**（入口字面注释，parser 与测试仍在）⇒ 非过期；
2. ：**生产在用**（被 contract/step_plan.hpp include）⇒ 非过期；
3.  与 ：**删除后的回归守卫测试**（tests 中断言不得回归）⇒ 非过期；
4. ：**机制已删**，仅存于删除说明注释与 README ⇒ 属「沿革」，非「现行描述」。

**判据（本轮确立）**：先用  判「生产路径是否仍使用」，**再**读正文判甲/乙/丙；**命中数不是过期信号**。

## 十、批次 3 评估记录

| 文件 | 判定 | 动作 | 理由 |
|---|---|---|---|
| docs/analysis/s4-r2-implementation.md | **已实行 ⇒ 归档**（原判「丙 ⇒ 留住」**已被本裁决取代**，保留作沿革） | **已移动** ⇒ docs/archive/20261005-1219-s4-r2-implementation.md | 已实行的 implementation 记录（政策第 1 条）；前半已废、后半现行内容由契约 3.20 承载 |

**本批归档 1 件**（docs/analysis/*.md 29 -> 28；docs/archive/*.md 55 -> 56；实测量产物）。

### 归档政策（2026-10-06 Lead 裁决，**优先于「丙 ⇒ 留住」**）

1. **implementation 记录**（s4-r2-… / s4-r3-… / s4-r6-… 一类「某子步骤已落地」的日志）⇒ **判「已实行」⇒ 归档**；
2. **例外（不归档）**：① 仍有 **open 项**（open>0）；② **被在途工作引用**；③ **仍是现行契约/机制权威**（如 contract-design.md、wire-transport-model.md、step-* 现役契约）；
3. **丙（混合）** 按 §九 处理（留住 + 标注），**但若整件本质是「已实行的记录」⇒ 按第 1 条归档**（**本条优先**）。


**待 Lead 裁**：是否把「已实行的 implementation 记录」（s4-r2/r3/r6 系列）统一归档 ⇒ 若采纳，本件改判归档。

## 十一、批次 3 收口（1 件归档）

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| docs/plan/vr-guard-plan.md | docs/archive/20261006-1809-vr-guard-plan.md | **已实行**（done/open=12/0；点名提交均存在；生产路径仅剩删除说明注释） | 2026-10-06 |


### 判据固化（2026-10-06）：`- [ ]` 的语境

- **`- [ ]` 只在【任务清单】语境下表示「未完成」**；在【约束 / 规范 / 检查表 / 指南】语境下**不表示未执行** ⇒ **按其内容性质归类**（例：`docs/analysis/ancillary-controller-guide.md` 的 `- [ ]` 是编码约束 ⇒ 判「现行」留住）。


## 十二、批次 3 收口（s4 三份 implementation 记录，3 件）

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| docs/analysis/s4-r1-implementation.md | docs/archive/20261006-1809-s4-r1-implementation.md | 已实行（① ② 不适用；③ 产物核验通过） | 2026-10-06 |
| docs/analysis/s4-r3-implementation.md | docs/archive/20261006-1809-s4-r3-implementation.md | 同上 | 2026-10-06 |
| docs/analysis/s4-r4-implementation.md | docs/archive/20261006-1809-s4-r4-implementation.md | 同上 | 2026-10-06 |


## 十三、批次 3 第 2 件（1 件归档）

| 原路径 | 归档路径 | 原因 | 日期 |
|---|---|---|---|
| docs/analysis/43284-logging-and-plugin-log-design.md | docs/archive/20261006-1809-43284-logging-and-plugin-log-design.md | **设计已落地**（diag_line.hpp 与 GLK_CAP_LOG 能力位均在生产路径；4 处命中属非过期例外） | 2026-10-06 |


### 政策补充（2026-10-06，Lead 裁决）：【待落地项】≠【过时片段】

- **计划里「将新增/将创建」且全仓 find 无同类的名字 ⇒ 判【待落地项】** ⇒ 属**未执行**，本件仍现行 ⇒ **留作路线图，不标注**；
- **曾有、现已改名/并入/删除的名字 ⇒ 判【过时片段】** ⇒ 按「丙」追加「归档评估」节（不改正文）；
- 三类事实须**分别记录**：**已迁移**（写清新位置）/ **已改名**（写清新名）/ **已删除**；不得笼统写作「过时片段」。

### 引用写法（2026-10-06，两次真实事故后固化）

- **写文档引用一律用 tools.edit 或临时文件**；**禁止把带反引号的路径放进 shell 载荷**（本会话已因此踩两次：① 两条互链的路径被命令替换吞成空 ② MASTER-PLAN 多处引用文本被吞 ⇒ 出现「见 」「：、」等空引用）。
- 改完**必须**跑空引用自查：grep -cE '见 *$|：、|① +的|其 +非任务' <file> ⇒ 应为 0。

