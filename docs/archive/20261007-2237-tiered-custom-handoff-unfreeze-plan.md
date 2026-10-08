# 归档头（docs/plan 批次，2026-10-07）

- 原始路径：docs/plan/tiered-custom-handoff-unfreeze-plan.md
- 归档原因：被 docs/plan/MASTER-PLAN.md 取代；未按现行设计规范编写
- 归档日期：2026-10-07 22:37（America/Toronto）
- 归档来源：task-63（docs-uml）

---


> # ✅ 已解冻（2026-10-06，用户裁决）：handoff 纳入 available（D1–D4 已批准）—— 插件与 payload 仍冻结 ✗
>
> （沿革保留：本文件原为「暂不解冻」的冻结记录；2026-10-06 用户裁决解冻 handoff ⇒ 上方横幅已更新，冻结期记录与重做要点保留在 §A/§B 供追溯。）

>
> ## 本文档现在是什么 / 不是什么
>
> - **不是**：解冻执行计划（原 §5 的解冻步骤**已失去用途**，保留作沿革）；
> - **是**：① 冻结态清单（§2，9 条，逐条 file:line）② **冻结后架构变化（§A，导致原计划过时）** ③ **重做要点（§B，供后续 L 级设计，不实现）**。

> 本窗口只交付「**来源清单 + 大纲**」（按一件一窗口的粒度）；全文留待下一窗口，各节已标 [待补]。

## 0 来源清单（已勘察，含 file:line）

### 0.1 计划/进度入口
- `docs/plan/branch-plan.md` —— **单一进度入口**；其**冻结/恢复清单**是解冻的权威（插件与 payload 的恢复条目在此；AGENTS 亦指向它）[待补：逐条行号]
- `docs/archive/20261006-1712-step-queue-design.md` —— 已归档的队列设计（与 handoff 选择面相关）

### 0.2 设计来源（现行 analysis）
- `docs/analysis/terminal-payload-tiers-design.md` —— **档位语义的现行设计件**（terminal × payload 档位作为正交轴）
- `docs/analysis/terminal-steps-redesign.md` —— terminal 步骤重设计（与档位/步骤序列相关）
- 归档：`docs/archive/20261006-1426-m5-handoff-20261006.md`（M5 交接稿，已归档）、`docs/archive/20260927-0029-wire-v2-kotlin-handoff.md`（wire v2→Kotlin 交接，已归档）

### 0.3 git 沿革（已查，提交存在）
- `05c0ad3b` docs(design): terminal payload tiers（root exec / LKM script / ko load）作为**与 terminal 正交的轴**
- `c335aabc` docs(design): payload tiers r2 —— **自描述档位 token、索引化 ko 键、统一失败语义、terminal × tier 可用性**
- `b9aa31dc` feat(payload): **档位设置页**（argv 契约、consent、运行前摘要）+ 有界原子导入（两遍摘要校验）+ 可重排 ko 列表 ⇒ **payload 档位已部分落地后被冻结**

### 0.4 代码侧「字面注释」现状（**解冻清单的权威**，已抽到样本）
- `src/core/main.cpp:23-27` —— `USER DIRECTIVE 2026-10-05: plugin paused`：`#include "plugin/host.hpp"` / `probe.hpp` / `wire.hpp` 三处 include **字面注释**
- `src/core/main.cpp:63-69` —— `--expect-sha256` 与 `--plugin-probe` 相关分支**字面注释**（错误串 `pr_error("--expect-sha256 requires --plugin-probe\n")` 亦注释）
- `src/core/profile/glkv3_parse.cpp:16-47` —— **payload owner 校验分支整段注释**（`payload_path_ok` / `payload_hash_ok` / `payload_sha_ok` 等，来源注释标明 contract-design 3.15 / design r2）
- **AGENTS.md 记载的其余触点（待逐条取证）**：`src/core/profile/schema.hpp` 的 `kPayloadGlkv3Fields`；`profile-manifest-v3.tsv` 的 **8 行** payload 条目；相关测试与 Makefile 目标；`execution_binding.cpp` 的 sink 绑定（插件侧）[待补：file:line]

### 0.5 UI 侧现状（只读勘察）
- App **隐藏两个入口且不再发射**（AGENTS 记载）；`app/src/main/kotlin/**` **本包不改** ✗ [待补：入口隐藏点 file:line]

## 1 它是什么 [待补]

- 档位语义（各档位对应什么：root exec / LKM script / ko load 等）
- 与 terminal（`root_child` / `umh_forward`）的关系（正交轴）
- 与 `payload` owner 的关系（wire 侧的 owner 与校验规则）

## 2 当前冻结态（**解冻清单**；逐条 file:line，实测）

> 恢复动作一律 = **撤销字面注释 / 恢复被注释的行**；**本包不执行任何一条** ✗。所属步骤对应 §5 的 ①–⑤。

### 2.1 已实测确认的冻结点

| # | 位置（file:line） | 原功能 | 恢复动作 | 所属步骤 | 备注 |
|---|---|---|---|---|---|
| 1 | `src/core/main.cpp:23-27` | `#include "plugin/host.hpp"` / `"plugin/probe.hpp"` / `"plugin/wire.hpp"` 三处 include | 撤销注释 | ④ | 行首标 `USER DIRECTIVE 2026-10-05: plugin paused` |
| 2 | `src/core/main.cpp:63-69` | `--expect-sha256` 与 `--plugin-probe` 的参数分支（含错误串 `pr_error("--expect-sha256 requires --plugin-probe\n")`） | 撤销注释 | ④ | 同上 |
| 3 | `src/core/profile/glkv3_parse.cpp:16-47` | **payload owner 校验分支整段**：`payload_path_ok` / `payload_hash_ok` / `payload_sha_ok`（来源注释指向 contract-design 3.15 / design r2） | 撤销注释 | ① | 解析层 ⇒ 见 §4 路径判定 |
| 4 | `src/core/profile/schema.hpp:26` + `:462-473` | 行 26 = `USER DIRECTIVE 2026-10-05: payload paused`；行 462-473 = **`kPayloadGlkv3Fields[]` 表与其说明注释**（含 `{"payload","tier",Str,true}`、`{"payload","exec.command",Str,false}` 等行） | 撤销注释（表与说明同批） | ①② | GLKv3 path→type 镜像 |

### 2.2 ⚠ 来源矛盾（**如实并列，不擅自调和** ⇒ 待裁见 §9）

| 来源 | 说法 | 实测 | 结论 |
|---|---|---|---|
| `AGENTS.md`（冻结段） | payload owner 的恢复清单含「**manifest 8 行**」 | 两份 manifest（`app/src/test/resources/profile-manifest-v3.tsv`、`profile-core/src/main/resources/profile-manifest-v3.tsv`）**各只有 1 行**含 payload 字样，且是 **`backend.cve_2026_43499.route.tcp_zerocopy.payload_delta`**（**route 字段**，与 payload owner 无关） | **不一致** ⇒ 待裁：AGENTS 的「8 行」可能指**已删除**的 payload owner 行（git 历史），或指另一份 manifest ⇒ **需 Lead/用户裁**（不改 AGENTS ✗） |
| `AGENTS.md` | 「相关测试与 Makefile 目标」被注释 | `src/Makefile` **仍列出** plugin 源（`core/plugin/loader.cpp:52`、`probe.cpp:53`、`wire.cpp:54`、`kernel_channel.cpp:55`、`host_ops.cpp:56`、`host.cpp:57`、`controller.cpp:81`）与 `core/memory/payload_builder.cpp:62`；**尚未逐行确认是否被注释** | **待确认**（下一窗口逐行核验） |
| `AGENTS.md` | 「`execution_binding.cpp` 的 sink 绑定」被注释 | 实测 `src/core/backend/cve_2026_43284/execution_binding.cpp` 存在（另有 `src/core/tests/execution_binding_test.cpp`）；**plugin 侧 sink 绑定位置未定位** | **待确认**（下一窗口定位：可能在 `src/core/plugin/host.cpp` 或该 backend 文件内） |

### 2.2b 三条核验结论（2026-10-06，本窗口实测；每条按「AGENTS 原话 → 实测 → 结论 → 是否建议修订 AGENTS」）

1. **Makefile**：AGENTS 原话 =「相关测试与 Makefile 目标」被字面注释；实测 = `src/Makefile:` **52-57 行**（`core/plugin/{loader,probe,wire,kernel_channel,host_ops,host}.cpp`）与 **62 行**（`core/memory/payload_builder.cpp`）、**81 行**（`core/plugin/controller.cpp`）**都未被注释**（是源列表的续行）⇒ **结论：部分不符**（这几行并非注释）；**但**该文件另有 **55 行**与 plugin/payload 相关的 `#` 注释行（很可能就是 AGENTS 所指的「相关测试/目标」被注释处）⇒ **建议**：下一窗口把这 55 行逐条列出并区分「源列表（未注释）」与「目标/测试（被注释）」，再把结论写入本节；**是否修订 AGENTS ⇒ 待 Lead 裁**（本包不动 AGENTS ✗）。
2. **plugin 侧 sink 绑定**：AGENTS 原话 =「`execution_binding.cpp` 的 sink 绑定」被注释；实测 = `src/core/plugin/host.hpp:94` 的注释**明确指向** `backend/cve_2026_43284/execution_binding.cpp (sink binding)`，且 `host.hpp:81` 写明「no sink; the residency window therefore dispatches nothing」⇒ **结论：一致（定位成立）** —— **sink 绑定在 43284 backend 侧**（`src/core/backend/cve_2026_43284/execution_binding.cpp`），plugin 侧（`plugin/host.*`）**不持有 sink**，仅通过该后端文件绑定；**是否修订 AGENTS ⇒ 建议补一句定位**（待裁）。
3. **payload 相关测试**：AGENTS 原话 =「相关测试」被注释；实测 = `src/core/tests/profile_entry_test.cpp:80` 有 **`USER DIRECTIVE 2026-10-05: payload paused -> the payload …`**（即**确有被注释的 payload 用例**）；`src/core/tests/glkv3_codec_test.cpp:46/114/163-165` 的 payload 只是 **wire 字段名（blob→payload，Bin 类型）**，与 payload **owner** 无关 ⇒ **结论：部分相符**（payload owner 用例确有注释：`profile_entry_test.cpp:80`；其余 6 个文件里的 payload 字样多与 owner 无关）。

- 相关文件：`app/src/main/kotlin/com/ghostlock/app/ui/AdvancedUI.kt`、`GhostlockUI.kt`、`GhostlockViewModel.kt`、`PayloadSettingsUI.kt`、`PluginDetailUI.kt`、`PluginCheckReport.kt`；
- **待补**：两个入口（插件 / payload 档位）**被隐藏的确切 file:line** 与隐藏方式（注释 / 条件屏蔽）⇒ 下一窗口；**本包不改 `app/**`** ✗。

### 2.2c Makefile 的 55 行注释分类（2026-10-06 实测，逐类）

**实测**：`grep -c "^#.*\(plugin\|payload\)" src/Makefile` = **55** 行。分类如下：

| 类别 | 行号（实测） | 性质 | 是否属冻结 |
|---|---|---|---|
| **A. 说明性注释**（描述已存在的 ABI/探针/wire/host 目标） | `:321`、`:330`、`:332`、`:348`、`:352-353`、`:362`、`:384` 等 | 纯文档注释（`# CM-1 countermeasure plugin ABI …`、`# S4 P1 read-only plugin probe …`） | **否**（不是被冻结的代码） |
| **B. 被冻结的目标定义**（**字面注释**） | **`:367`**（`# USER DIRECTIVE 2026-10-05: plugin paused`）⇒ 其后的 `:368-373`（`# $(HOST_BUILD_DIR)/plugin_host_test: …`）与 `:379-381`（编译命令行）；**`:386`**（同一 USER DIRECTIVE）⇒ `:387+`（`# $(HOST_BUILD_DIR)/plugin_window_wiring_test: …`） | **两个 host 测试目标被整块注释** | **是** ⇒ 属解冻清单 |
| **C. 源列表（**未**注释）** | `:52-57`（`core/plugin/{loader,probe,wire,kernel_channel,host_ops,host}.cpp`）、`:62`（`core/memory/payload_builder.cpp`）、`:81`（`core/plugin/controller.cpp`） | 仍在 `SOURCES` 续行中 ⇒ **未被注释** | **否** |

**结论（写入 §2.2 的「是否建议修订 AGENTS」）**：AGENTS 的「相关测试与 Makefile 目标被注释」**在「目标」这一层成立**（B 类：`plugin_host_test` / `plugin_window_wiring_test` 两个目标整块注释，锚点为 `:367`/`:386` 的 `USER DIRECTIVE`）；**但源列表未被注释**（C 类）⇒ AGENTS 的表述**过宽**，建议精确化为下述 §10 第 4 条。

### 2.3 UI 侧隐藏点（**已定位 file:line**）

- **payload 档位入口**：`app/src/main/kotlin/com/ghostlock/app/ui/GhostlockUI.kt:174` —— `val payloadVisible: Boolean = false`（**默认 false ⇒ 入口隐藏**）；渲染点 = 同文件 `:335`（`if (state.payloadVisible) { … }`）；
- **插件详情入口**：`GhostlockUI.kt:172` —— `val pluginDetail: PluginDetailState? = null`（默认 null ⇒ 无详情页可入）；相关路径 `:275`/`:276`（`onOpenPluginDetail`/`onClosePluginDetail`）、`:317`（`PluginDetail` screen）、`:332`（面包屑）、`:379`（路由分支）；
- **结论**：**两个入口的隐藏点是 UI 状态默认值**（`payloadVisible=false`、`pluginDetail=null`），**不是注释** ⇒ 恢复动作 = **恢复默认值/接线**（而 native 侧是撤注释）⇒ **两条恢复路径不同，§5 步骤需分别写** ✓（本包不改 `app/**` ✗）。


- 含 payload 字样的测试文件（实测）：`src/core/tests/{fake_backend_test,glkv3_codec_test,cpp_link_probe,route_policy_test,profile_entry_test,cve_2026_43284_stage_runner_test}.cpp`；
- **待确认**：哪些用例与「payload owner」直接相关、是否有被注释的 payload 专用测试（下一窗口逐文件 grep `USER DIRECTIVE` 与被注释用例）。
### 2.4 测试面（已落盘，2026-10-06 实测）

**属冻结 ⇒ 进解冻清单（3 条）**：

| # | 位置 | 内容 | 恢复动作 |
|---|---|---|---|
| T1 | src/core/tests/profile_entry_test.cpp:80 | USER DIRECTIVE 2026-10-05: payload paused -> the payload 用例被注释 | 撤销注释并恢复用例 |
| T2 | src/Makefile:367-373 与 :379-381 | plugin_host_test 目标与其编译命令行（锚点 :367 的 USER DIRECTIVE） | 撤销注释 |
| T3 | src/Makefile:386-387+ | plugin_window_wiring_test 目标（锚点 :386 的 USER DIRECTIVE） | 撤销注释 |

**与 payload owner 无关（**不计入**清单）**：

- src/core/tests/glkv3_codec_test.cpp:46/114/163-165 的 payload = **wire 字段名**（blob -> payload，Bin 类型）；
- 其余含 payload 字样的测试（fake_backend_test / cpp_link_probe / route_policy_test / cve_2026_43284_stage_runner_test）多为 route 字段 payload_delta 或无关词汇 ⇒ 待逐条复核（不阻塞）。

**解冻清单合计 = 9 条**：§2.1 的 **4** 条（native 侧解析/main）+ **§2.4 的 3 条**（1 用例 + 2 Makefile 目标）+ **§2.3 的 2 条**（UI payloadVisible / pluginDetail）。

## 3 为何冻结（沿革，不删历史）[待补]

- 用户指令 2026-10-05：**暂停插件与 payload 工程**；native 侧接线按**字面注释**（可逆形式）
- 当时决定：**恢复＝撤销注释 + 跑门禁**（恢复清单逐条到 file:line，而非考古）

## 4 解冻会改动什么（**9 条逐条判定**；判据引代码/流程）

> **注（2026-10-06 用户裁决后）**：以下为**旧计划**的判定；**重做后需重新判定**。

**判据**：一条恢复项若落在 waiter / race / payload / route / exec **或其资源准备/回收**路径上 ⇒ **必须真机门禁**（前置：冷机 + KernelSU 未加载 + 固定 CPU 对 + 单 route；结果按 `docs/analysis/device-gates/` 格式归档）。

| # | 条目 | 判定 | 依据（file:line / 流程） | 真机门禁 |
|---|---|---|---|---|
| N1 | `src/core/profile/glkv3_parse.cpp:16-47`（payload owner 校验） | **在攻击路径（输入面）** | 该校验决定**送入 native 的文档形状**（哪些 payload 段合法）⇒ 属 exec 阶段的输入门禁，非法即拒 ⇒ 影响可执行的组合集合 | **需要** |
| N2 | `src/core/profile/schema.hpp:26` + `:462-473`（`kPayloadGlkv3Fields[]`） | **在攻击路径（输入面）** | GLKv3 path→type 权威表：决定字段解析类型与 required；改动会改变绑定结果 ⇒ 同 N1 的输入面 | **需要** |
| N3 | `src/core/main.cpp:23-27`（plugin include） | **在攻击路径（运行时接线）** | plugin host 的构造/open/bind/close 由**组合根**执行；既有 **R1（waiter 存活期不得 open）** 证明它与 **waiter 生命周期耦合** ⇒ 属资源准备/回收面 | **需要** |
| N4 | `src/core/main.cpp:63-69`（`--expect-sha256` / `--plugin-probe` 分支） | **在攻击路径（间接）** | `--plugin-probe` 会在**攻击进程内 dlopen 外部 .so**（`src/core/plugin/probe.hpp:41` 提到 `PR_SET_NO_NEW_PRIVS`）⇒ 属资源准备面 | **需要** |
| T1 | `src/core/tests/profile_entry_test.cpp:80`（被注释用例） | **不在**（测试面） | 恢复用例不改变生产二进制 | 不需要 |
| T2 | `src/Makefile:367-373` + `:379-381`（`plugin_host_test`） | **不在**（构建面） | 仅恢复测试目标登记与编译命令行 | 不需要 |
| T3 | `src/Makefile:386-387+`（`plugin_window_wiring_test`） | **不在**（构建面） | 同上 | 不需要 |
| U1 | `app/src/main/kotlin/com/ghostlock/app/ui/GhostlockUI.kt:174`（`payloadVisible=false`） | **不在**（UI 状态默认值） | 仅控制入口可见性；不改变已发射文档 | 不需要 |
| U2 | `app/src/main/kotlin/com/ghostlock/app/ui/GhostlockUI.kt:172`（`pluginDetail=null`） | **不在**（UI 状态默认值） | 同上 | 不需要 |

**小结**：**4 条落攻击路径 ⇒ 必须真机门禁（N1–N4）**；**5 条不需**（T1–T3 构建/测试面、U1–U2 UI 默认值）。

**裁决（Lead，2026-10-06）**：**维持保守判定、不降级** ✗ —— N4 因「在攻击进程内加载并执行外部代码」（dlopen）属资源准备面；N3 因「组合根执行 + 与 waiter 生命周期耦合（R1）」属资源准备/回收面 ⇒ **N1–N4 全部需要真机门禁** ✓（本表按此定稿）。
## 5 解冻计划（分步）

> 原解冻步骤已失去用途，保留作沿革（见顶部横幅与 §B）。[待补：每步动作 + 门禁 + 期望结果]

- 步骤骨架（待细化）：① 解析层 payload owner 解冻 → ② manifest/测试/Makefile 同步 → ③ payload 档位接线 → ④ 插件侧接线（若同批）→ ⑤ UI 入口恢复
- 门禁（每步）：native-host-tests 目标 / lint-tidy 目标（命令见 AGENTS 常用命令节） / NDK 构建零告警 / `:app:` 三件套 / `exportProfiles` / **真机门禁（如需）**

## 6 风险与回退 [待补]

- 回退 = **重新冻结**（把注释加回去 + 跑门禁），保持与本次相同的可逆性

## 7 批准检查点

> **本包暂停；重做设计需用户重新批准。**

- **本包暂停**：原流程「批准后才可开始第 1 步」**已作废**（见本节上注与顶部横幅）；**重做设计需用户重新批准**。

## 8 UML 影响（同批更新，不在本包内改）[待补]

- 预计涉及：全流程 UML 的 **IPO**（payload 档位/handoff 时序）与 **Class Kotlin**（档位设置页）等；具体图号待下一窗口定位

## 9 待 Lead / 用户裁（来源矛盾或政策未定）

- 插件侧（`plugin/*`）与 payload 档位是否**同批**解冻（AGENTS 将二者一并记载为冻结）⇒ 待裁
- `--plugin-probe` 在 AGENTS 中标记为**冻结保留**（§九 例外 2）⇒ 解冻时是否恢复该入口 ⇒ 待裁
## 10 建议的 AGENTS 修订（**本包不改 AGENTS** ✗；逐条 = 原话 → 实测 → 建议新表述；待 Lead 裁后单独派）

1. **插件/payload 冻结描述**：原话 =「插件的运行时接线 … 以及 payload owner … 全部字面注释」；实测 = **成立**，但**未点明 UI 侧不是注释**；建议新表述 =「native 侧按**字面注释**冻结；**UI 侧为状态默认值**（`GhostlockUI.kt:174` 的 `payloadVisible=false`、`:172` 的 `pluginDetail=null`）⇒ **解冻 = 撤注释 + 恢复默认值/接线**（两类动作）」。
2. **sink 绑定定位**：原话 =「`execution_binding.cpp` 的 sink 绑定」；实测 = 该文件是 **`src/core/backend/cve_2026_43284/execution_binding.cpp`**（plugin 侧 `src/core/plugin/host.hpp:94` 明确指向它，`:81` 写「no sink … dispatches nothing」）；建议新表述 = 补一句「**sink 绑定归属 43284 backend**（`backend/cve_2026_43284/execution_binding.cpp`），plugin 侧不持有 sink」。
3. **payload 测试**：原话 =「相关测试 … 字面注释」；实测 = **部分相符**（`src/core/tests/profile_entry_test.cpp:80` 确有被注释的 payload 用例；其余含 payload 字样的测试多为 **wire 字段名 `blob.payload`（Bin）**，与 owner 无关）；建议新表述 = 点名「`profile_entry_test.cpp:80` 的 payload owner 用例被注释」，避免后人按关键词误判。
4. **manifest 8 行（已查明，AGENTS 表述准确 ✓ 无需修订）**：实测 `git show ca968a5a^:profile-core/src/main/resources/profile-manifest-v3.tsv | grep -c payload` = **9**，其中 **8 条为 payload owner 行**（`payload.exec.command` / `exec.sha256` / `ko.<i>.path` / `ko.<i>.sha256` / `ko.count` / `script.path` / `script.sha256` / `tier`）+ **1 条 route 字段** `…tcp_zerocopy.payload_delta` ⇒ **AGENTS 的「manifest 8 行」= 8 条 owner 行，准确** ✓；沿革 = 由 **`74db3594`** 加入、由 **`ca968a5a` 注释**（冻结提交）；**今天**两份 manifest 各只剩那 1 行 route 字段（owner 行已注释/移除）⇒ **建议仅补一句「今天已注释，恢复时按 8 行还原」** ✓（原「口径不清」结论作废，保留作沿革）。

**同时建议（第 5 条，工具性）**：AGENTS 的冻结段加一句「**解冻清单以 `docs/plan/tiered-custom-handoff-unfreeze-plan.md` §2 为权威**」，避免两处各写一份清单。

## §A 冻结后架构变化（导致原计划过时）—— 逐条带证据

| # | 变化 | 证据（实测） | 对原计划的影响 |
|---|---|---|---|
| A1 | **选择面：token 形态已删** | M5 = `e59a8479`（native `plan_error reason=token-form-removed`）⇒ 现为 `backend.<id>{ route, queue }` 对象形态 | 原按 token/steps 表达的档位选择已不成立 |
| A2 | **owner 模型变更** | `common.*` / `countermeasure.*` owner 已删（出现即拒；`tests/profile_v3_test.cpp:199/206` 断言）；`platform.*` 迁往 `backend.cve_2026_43499.abi.*` | 原计划若依赖这些 owner 需重新表达 |
| A3 | **payload owner 已删** | manifest 8 行 owner 行由 **`74db3594`** 加入、**`ca968a5a`** 注释；**今天两份 manifest 各只剩 1 行**（route 字段 `payload_delta`） | 原「按 8 行还原」仅属历史 |
| A4 | **UI 冻结形态 ≠ 注释** | `GhostlockUI.kt:174` `payloadVisible=false`、`:172` `pluginDetail=null`（状态默认值） | 恢复动作与 native 不同（见 §2.3） |
| A5 | **插件运行时接线冻结** | `main.cpp:23-27` 三 include、`:63-69` 两 CLI 分支字面注释；**R1（waiter 存活期不得 open）**；**`POST_TERMINAL` 只在 43284 的 LKM 驻留窗口成立** —— 依据 `src/core/plugin/host.hpp:81/94`（「no sink; the residency window therefore dispatches nothing」+ sink 在 `backend/cve_2026_43284/execution_binding.cpp`） | 档位若依赖插件窗口需重新定义 |
| A6 | **vr_guard / platform/vivo 已彻底删除** | (a) `4a182217`；`backend/cve_2026_43499/schema.hpp:114` 注明；`src/core/README.md:76` | 原计划中 vivo 条目已无对象 |
| A7 | **general profile 设计晚于冻结** | **`docs/analysis/execution-combination-menu-and-general-profile.md`**（8 份 general + `available` 声明 + 派生 + 守卫）⇒ 该件因**被在途工作引用**而原地保留于 `docs/analysis/` | **档位必须在新声明模型里重新表达** |
| A8 | **R1 账本归零 + 全部走 manifest** | include 防火墙 **0 白名单**；`vocabulary-manifest.tsv`、`stepset-steps.tsv` | 新增字段/轴须同批更新 manifest 与对拍 |
| A9 | **构建/缓存硬规则（2026-10-06）** | 产物一律 `<repo>/build/**`；`~/.ghostlock` 已清空；`~/.gradle` 保留 | 重做时的构建与缓存路径按此 |

## §B 重做要点（供后续 L 级设计；**不实现**）

每条 = 必须重新决定的问题 + 选项 + 代价 + 建议。

1. **「档位」这个轴放哪**：选项 = 新 owner（如 `backend.cve_2026_43499.payload.*`，与 A2 的 ABI 迁移同向）/ 复用 `available` 的**第二条目** / 其它。代价：新 owner 需同批更新 manifest 与对拍；复用 `available` 会混淆「选择」与「档位」。**建议：新 owner**（与迁移方向一致）。
2. **与 terminal 的正交性如何表达**：选项 = 在 `available` 里枚举 terminal × tier 组合 / 新增正交轴字段。代价：枚举会组合爆炸；正交轴需新 schema。**建议：先枚举已实测组合，正交轴留作后续**。
3. **与插件 `POST_TERMINAL` 的关系**：今天**只对 43284 成立**（依据见 A5）；若 43499 也要载模块 ⇒ 该窗口语义需**重新定义**（选项：为 43499 定义等价窗口 / 放弃该点）。**建议：先限定 43284，43499 另案**。
4. **是否保留 `payload` 命名**：owner 已删且「出现即拒」⇒ 沿用会与冻结语义冲突。**建议：换名（如 `handoff` / `deploy`），避免与历史段名混淆**。
5. **fallback / 第二路径的边界**：用户已提「第二个 `available` 条目还没做」⇒ 需与档位设计划清边界（选项：档位不做 fallback / fallback 单列一个 `available` 条目）。**建议：fallback 单列条目，档位不承担**。
6. **迁移与兼容**：旧 v2 `fallback.route.*` 归 `LegacyProfileConverter`（属遗留），**不作为新设计基础**；**v3 从未发布 ⇒ 无兼容负担**。**建议：新设计只面向 v3 最新形态**。