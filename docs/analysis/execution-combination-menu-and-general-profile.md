# 执行组合子菜单 + 8 份 general profile + 新加载判据（L 级设计稿 v2）

> 状态：**设计稿（未实现）**。v2 于 2026-10-06 依据用户最新裁决修订（见 §0.2）；仍未确认的点集中在 §4。
> 本文只写设计，**不含任何代码/资产改动**。相关：`docs/archive/README.md（已归档设计稿索引）`、`docs/plan/branch-plan.md`、契约 §3.20。

## 0 用户原话

### 0.1 首轮原话（逐条，语义不得改写）

1. 「修改执行组合：将执行组合变成一个子菜单，需要进去选择。」
2. 「这些路径都要本地化，并且只可展示或选择配置标记 available 的」
3. 「我的 a301so 还能选 pselect 和 tcp，这不正确。」
4. 「修改 template profile 改为 general profile，将其增加至 8 个对应的 lkm，里面默认启用 43284 backend（原文写作「42384」，**待确认**），并将已有的 5.15、6.1、6.6、6.12 的空 43499 模版写进去，但是声明 available 的地方将 43499 的可用性声明注释掉。」
5. 「没有精确匹配内核的需要加载 general profile。」
6. 「将 kernel_profile 更名为 profile。」

### 0.2 最新裁决（2026-10-06，逐条照录，**取代**首轮中的相关表述）

1. 「不要并入一个 general profile，而是八个 lkm 库各对应 8 个 general profile，其中不是 5.15/6.1/6.6/6.12 的不写 43499 的模版」
2. 「U5 是我口误，我的意思是 cve_2026_43284」
3. 「a301so 的 43499 只声明 mcast，但是同时加入 43284 声明，并把其他内置 profile 也整理并加入默认 43284 声明，因为 43284 声明更加简单易用」
4. 「未精确匹配即拒绝运行是旧规则，新规则为检查选择的后端/路径的必须参数是否填写（不包括可自动推断的），同时配置编辑中可自动推断的项缺失时要明确提示，颜色要黄色而不是红色」

## 1 现状与事实（含 file:line）

### 1.1 可用性当前来自编译期目录（这是 (3) 的根因）

- `app/src/main/kotlin/com/ghostlock/app/ui/CombinationPresentation.kt:26`：`enabled = it.available`；
- `app/src/main/kotlin/com/ghostlock/app/ui/GhostlockUI.kt:110`：不可用后端置灰；
- `app/src/main/kotlin/com/ghostlock/app/ui/GhostlockViewModel.kt:1283`（`if (!kind.available) return`）、`:1289`（`CombinationCatalog.availableForBackend`）；
- ⇒ **UI 与 profile 的 `available` 声明无关** ⇒ 设备只跑通 multicast，却仍能选 pselect/tcp（**不正确**）。

### 1.2 两个「可用性」是不同的东西

- `selection_supported()`：**设备已核实**（真机跑通）——用户 (2)(3) 的诉求；
- `combination_supported()`：**已接线**（有编译期 Pipeline 与 orchestrator case）——只说明路径存在；
- ⇒ UI 门禁口径必须是「**profile 声明 ∩ 设备核实**」。

### 1.3 profile 资产与选择路径

- 资产目录 `app/src/main/assets/profile/`（**68 个 `.conf`**：release profile + 4 个 template + 共享片段 + `index.conf`）；
- `index.conf`：`backends = [ { id, usable } ]`（**构建/资产层**）与 `profiles = [ { release, file } ]`（release → 文件）；
- 5.15 设备 profile 现状（`5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf:5-10`）：只声明 `available { cve_2026_43499 { route = "multicast_waiter"; queue = [w1,w2,w3] } }`；
- 选择入口：`AndroidProfileConfigController.load()`（`app/src/main/kotlin/com/ghostlock/app/data/AndroidProfileConfigController.kt:95`）→ `resolveCurrent()`（`:742`）→ `resolve()`（`:752`）；**取不到 ⇒ `hasProfile = false`**（`:100-102`）⇒ 目前**未精确匹配即不可运行**；
- 运行侧拦截：`AndroidGhostlockRepository.kt:549`（`!config.hasProfile || profileBlob == null -> …`）、`GhostlockViewModel.kt:218/1650`（`executionHasProfile`）。

### 1.4 native 是否参与 release 匹配（已核实：不参与）

- `src/core/profile/glkv3.cpp:413`、`glkv3_parse.cpp:555`：native 只把 `release` 当**文档字段**解析/携带，**没有任何 release 与设备比对**；
- ⇒ **general 回退与新加载判据都可以在纯 Kotlin 层完成**；native 不需要改（实现批次仍需以真实调用链复核一次）。

### 1.5 LKM label ↔ 内核族（8 个 label 的权威来源）

- **单一真相**：`lkm-kmi-manifest.tsv`（native 导出；两份逐字节一致：`app/src/test/resources/lkm-kmi-manifest.tsv` 与 `profile-core/src/main/resources/lkm-kmi-manifest.tsv`；生成命令见 AGENTS 的「stepset → 步骤序列 manifest」同规条目（native 侧导出任务））；
- 列 = `label / android_release / kmi / ko_filename`，**8 行**：`android12-5.10`(12/5010)、`android13-5.10`(13/5010)、`android13-5.15`(13/5015)、`android14-5.15`(14/5015)、`android14-6.1`(14/6001)、`android15-6.6`(15/6006)、`android16-6.12`(16/6012)、`android17-6.18`(17/6018)；
- **manifest 自带不变量（原文）**：`android12-5.10` 与 `android13-5.10` **同 kmi 5010**；**label 是交付身份，文件名跟随 label，绝不跟随 KMI**；
- ⇒ **内核族**（用户口径的 5.15 / 6.1 / 6.6 / 6.12）= manifest 的 `label` 去掉 `android<api>-` 前缀所得的 `major.minor`；**必须由 manifest 派生，禁止手抄**。

### 1.6 本地化机制

- `app/src/main/res/values/strings.xml`（**369 个 string**）+ `app/src/main/res/values-zh/strings.xml`；已有可用性文案（例 `R.string.root_manager_unavailable`）；新增可见文案一律走该机制。

### 1.7 改名影响面（初查 **143 处**命中）

- 资产目录名、`index.conf` 自引用与 `profiles[].file`、Kotlin/测试里的 `profile/...` 加载字符串（`M3ByteDeltaInventoryTest`、`ProfileLayoutEquivalenceTest`、`BuiltinProfilesTest`、`BackendMatrixAgreementTest`、`StepQueueAssetMigrationTest`、`ExporterAgreementTest` 等）、Gradle 任务 `exportProfiles`、`docs/profile/**`、`docs/development/**`、`AGENTS.md`、`README(_ZH).md`、v1 夹具 `profile-legacy/`。
## 2 逐条设计（选项 + 代价 + 建议）

### 2.1 (1) 子菜单的信息架构

- **选项 A：新页面（导航目的地）**——「执行组合」控件改为入口行，点进去是独立页面（后端 → 路径）。代价：需要导航/返回语义与状态恢复；收益：层级清晰，适合本地化路径名与「声明驱动可用性」。
- **选项 B：对话框/底部弹层**——轻量。代价：层级多了会拥挤；收益：改动面小。
- **建议：A**（用户明确「**需要进去选择**」）。
- **未选择时的默认值**：候选 ① 保持当前 `state.backendKind`/`combination` ② 强制选择后才允许开打 ③ 自动选唯一可用项 —— **待确认 U1**；
- **状态机影响**：`state.backendKind`、`state.combination`、`setBackendKind()`（`GhostlockViewModel.kt:1283`）仍是**唯一选择权威**；子菜单只改呈现与筛选，**不新增第二份选择状态**；返回语义 = 取消未确认的选择（默认不写回）。
#### 2.1.1 配置形态 = **扁平注册**；UI = **从扁平派生树**（U17 已裁决 2026-10-06）

**用户原话（照录）**：「**U17：hocon 配置内可以有扁平化 `available` 注册**，例如 **43499 可以注册多个不同 step 的项**，用树也可以但是**可能缺乏维护性和可读性**；但是如果采用扁平配置，**UI 如何生成优雅的树状选项需要思考**」。

**① 配置侧（裁决）**：**一个条目 = 一条完整路径**（`backend` + `route` + `queue`/steps + `terminal` + `priority`）；**同一 backend 允许注册多条**（用户示例：`43499` 的 mcast 下三条 = `w1,w2,w3,root child` / `w1,w2,shizuku` / `w1,w2,umh(未实现)`）；
**② UI 侧（裁决）**：树**从扁平条目派生**（backend → route → 叶子 = 步骤序列 + terminal），**树层级不在配置里表达**；

**形态问题（HOCON 对象键唯一 ⇒ 现状 `available { <backend> { … } }` 每个 backend 只能出现一次）——三条候选（并列，不预选）**：

- **(a) 对象列表（建议）**：

```hocon
available = [
  { backend = "cve_2026_43499", route = "multicast_waiter", terminal = "root_child",
    queue = [ { step = "w1" }, { step = "w2" }, { step = "w3" } ], priority = 2 },
  { backend = "cve_2026_43499", route = "multicast_waiter", terminal = "shizuku",
    queue = [ { step = "w1" }, { step = "w2" } ], priority = 3 },
  { backend = "cve_2026_43499", route = "multicast_waiter", terminal = "umh_forward",
    queue = [ { step = "w1" }, { step = "w2" } ], priority = 4, planned = true }
]
```
  ⇒ 天然支持同 backend 多条、**优先级显式**、**无需造 id**；**代价 = 必须放开「列表形态一律拒绝」**（但**继续拒绝字符串列表**，见下）。
  ⚠ **与 M5 的关系（用户已纠正过一次）**：M5 拒的是 **token 列表（字符串形态）**，理由「队列取代 token」；**对象列表是另一种东西**，与 M5 理念不冲突 ⇒ 采用 (a) 需要放开列表拒绝，**但必须保住 M5 的意图**：**对象列表 ⇒ 接受；字符串/其它形态 ⇒ 具名拒绝**（沿用 M5 的诊断措辞风格，例如 `available: the token-list form was removed in M5; declare objects with backend/queue` 与 `available: string entries are not a selection; declare an object with backend/queue`）。
- **(b) 任意 id 作键**：`available.<id> { backend = …, route = …, queue = [ … ] }` ⇒ 简单、HOCON 原生；**但 `id` 相当于把 token 换个名字**（M5 刚删掉 token 概念）⇒ **不建议**；
- **(c) 每 backend 下再嵌变体（树）**：可表达（`available.<backend>.<route>.<variant> { … }`），但**正是用户判定可读性/维护性差的那种** ⇒ **记录但不建议**。

**选定后的影响面（三候选共用的核对清单，逐条 file:line）**：
- **Kotlin 解析/校验**：`profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:420`（`validateAvailable`）与 `:453`（`validateAvailableSelection`）；若走 (a) 还要改 `:435`/`:438` 的**列表拒绝**语义；
- **M5 的两条具名诊断与其测试**：`ProfileLayout.kt:435`（token-list）/`:438`（empty token list）+ `profile-core/src/test/kotlin/com/ghostlock/app/data/ProfileLayoutAvailableTest.kt:31/:36`；
- **golden / fixture 与资产**：`profile-core/src/test/resources/glkv3-native-fixture.tsv`、Kotlin golden（现 3920 字符）与 native 导出的 golden 十六进制（生成任务见 AGENTS 的 manifest 生成约定）；**76 份**（68 release/片段 + 8 general）资产里的 `available` 段；`index.conf`（general 登记，见 U4b）；
- **native 侧（已核实）**：`available` **不是**执行输入——native 只按 wire 的 `backend.<id>` 与 `queue`/`route` 绑定（契约 §3.20）；native 侧的 `available` 仅是**编译期组合目录**的字段（`src/core/pipeline/orchestrator.hpp:41-52`、`component_catalog.hpp:29-69`），**与 profile 的 `available` 段无关** ⇒ **本裁决不影响 native** ✓。

**UI 树生成规格（用户要求「思考」的点，写成可实现规格）**：
1. **分组**：一级 = `backend`；二级 = `route`（43284 无 route 轴 ⇒ 二级退化为该 backend 自身，需在实现时明确）；叶子 = `queue`（步骤序列）+ `terminal`；**同一 (backend, route) 的多条 = 兄弟叶子**；
2. **排序**：叶子按 **`priority` 升序**（= 默认项在前）；**同级并列时用确定性次序**（如步骤序列字符串 + terminal 名的字典序）⇒ 避免「看起来随机」；一级/二级建议按**既有目录顺序**（`CombinationCatalog`/词汇 manifest 的顺序，保持与其它 UI 一致）；
3. **标签（本地化）**：术语来源**只用既有词汇**（backend / route / terminal / 步骤名），**不新造词**；叶子推荐格式：「**w1 → w2 → w3 → 根权限**」/「**w1 → w2 → shizuku**」（中英各一套，键见 §2.2 的 `execution_combo_*` 系列）；
4. **「未实现」节点**：用户示例含 `umh(未实现)` ⇒ 必须能表达「**已声明但当前不可运行**」；选项：① **条目内加 `planned = true`**（建议：语义局部、与 `priority` 同级、易校验）② 由 `index.conf` 的 `usable` 表达（**不建议**：那是构建/资产层，粒度是 backend 不是路径）③ 单独的 planned 列表（**不建议**：第二处真相）；
   ⚠ **与 U2「以声明为准」的关系（必须写清）**：`available` 里的条目 = **声明可用** ⇒ **「未实现」的条目不应写进 `available`**；若用户希望「先占位、后实现」，则用 **`planned = true`** 明确标注「**声明了但当前不可运行**」，UI **可见但不可选**并显示「未实现」——**这与 U2 不冲突**：U2 管的是「可用项以声明为准」，`planned` 管的是「声明但未实现」的**显式降级标注**；
5. **未知形状必须 fail-visible**：配置里出现 UI/校验表不认识的 backend/route/terminal/步骤 ⇒ **报错误项（红）**，**不得静默隐藏**（与前次裁决一致）；
6. **证伪**：① 同一 backend 注册**多条** ⇒ 树里必须是**三个叶子**（不是一条、也不是三个平铺的一级项）；② **打乱配置顺序** ⇒ 树**不变**（由 `priority` 决定）；③ **未实现条目** ⇒ **可见且有「未实现」标注**（且不可选）。

#### 2.1.2 菜单生成策略（待 Lead 定的 UI 策略；已给建议）

用户原话：「具体配置文件里怎么样应当保持灵活性/低复杂度；但是 UI 选项我不知道怎么选：根据配置生成菜单 / 预制菜单种类，检测到 available 即加载」。

| 维度 | 路线 A：**配置生成菜单**（数据驱动） | 路线 B：**预制菜单种类**（代码内固定形状）+ 检测到声明即加载 |
|---|---|---|
| 数据来源数 | **1**（profile 的 `available` + requirements 文件夹） | **2**（代码里的形状表 + profile 的声明）⇒ 双真相 |
| 漂移风险 | 低（形状随声明走） | **高**（新增路径要同时改代码与配置；忘记改一边 ⇒ 菜单与可执行不一致） |
| 新增路径成本 | 只改配置（+ requirements 条目） | 改代码 + 配置 + 发版 |
| 未知形状（配置里出现代码不认的组合） | **fail-visible**：报「未知形状」错误项（红） | 静默忽略或崩溃 ⇒ 用户看不到新路径 |
| 与「低复杂度配置」的关系 | 配置保持最小语法（§2.1.1） | 需要维护两套形状定义 |

- **采用建议（Lead 已给）**：**路线 A（数据驱动）** + **requirements 文件夹作为「形状来源」**（§2.12）+ **未知形状必须 fail-visible**（红，阻断运行）。


### 2.2 (2) 本地化范围（含新判据的红/黄提示语）

- **列表项**：后端名、路径摘要（现由 `CombinationPresentation.kt` 的 `combinationSummary` 生成 ⇒ 改为 string 资源 + 占位符）；
- **导航与页面**：入口名、页面标题、面包屑、返回、空态、不可用原因；
- **新增键（建议前缀 `execution_combo_*`，中/英各一条）**：

```text
execution_combo_title              执行组合 / Execution combination
execution_combo_subtitle           仅显示配置声明为可用的路径 / Only paths declared available
execution_combo_backend_header     后端 / Backend
execution_combo_path_header        路径 / Path
execution_combo_unavailable_hint   未在配置中声明可用 / Not declared available in the profile
execution_combo_no_profile_hint    尚未加载配置 / No profile loaded
execution_combo_selected           当前选择 / Selected
execution_combo_confirm            使用该组合 / Use this combination
execution_combo_cancel             取消 / Cancel
execution_combo_unverified_hint    未验证路径，请谨慎使用 / Unverified path, use with care
```

- **新加载判据（§2.9）的红/黄提示键（建议前缀 `profile_check_*`）**：

```text
profile_check_missing_required     缺少必须参数：{0} / Missing required parameter: {0}               红色，阻断运行
profile_check_derivable_missing    可自动推断：{0}（运行时会推导） / Derivable: {0} (resolved at runtime)   黄色，不阻断
profile_check_backend_unrunnable   该后端/路径的必须参数不完整 / Required parameters for this backend/path are incomplete   红色，该行不可选
```

- **代价**：文案变更可能触及 UI 断言；**建议**：先加资源再接 UI。

### 2.3 (2)(3) 可用性判据：UI 读已加载 profile 的 `available` 声明

- **判据（优先级）**：① **profile 未加载** ⇒ 全部不可选（置灰 + `execution_combo_no_profile_hint`）；② **已加载** ⇒ 可选 = 「profile 的 `available` 声明」∩「编译期目录 `available`（已接线）」；③ 冲突 ⇒ **以 profile 声明为准**（**U2 已裁决 2026-10-06**）——**「代码里是否已接线」不代表本机支持**（`combination_supported()` 只说明路径存在，`selection_supported()` 才是设备已核实）；若有人**乱写 profile** ⇒ **在「加载后、攻击前」报出错误项（红色）**，见 §2.9-⑦；
- **数据通路**：现成对象 = `ProfileConfig`（`AndroidProfileConfigController.load(...)` 返回，含 `hasProfile` 与 `roots`）；建议**新增一个「已解析的可用组合集合」字段**，由 `AndroidProfileConfigController` 在解析时**从同一份 canonical 文档派生**（禁止第二处解析）；
- **降级**：`hasProfile == false`、解析失败、`available` 缺失 ⇒ 一律「不可选 + 文案」，**不得回退到编译期目录**；
- **证伪**：把 5.15 profile 的 multicast 声明注释掉 ⇒ 该行必须消失/置灰；恢复 ⇒ 必须可选（`--rerun-tasks` 强制重跑，不依赖增量判定）。
### 2.4 (3) a301so(5.15) 声明 + **全部内置 profile 整理**（裁决 0.2-3）

**a301so(5.15)**：43499 **只声明 `multicast_waiter`**（现状已如此，保持）+ **同时加入 43284 声明** ⇒ 该设备 UI 会同时出现 43284 的路径；
**所有内置 profile 一并整理并加入默认 43284 声明**（理由：43284 声明**更简单易用**）；
- **「整理」的可复核判定标准（逐条打勾）**：
  1. **声明齐全**：`available` 段存在；**43284 声明存在**；43499 声明**只含实测通过**的组合；
  2. **格式统一**：`route` 仅在 43499 下出现、43284 不得出现 `route`（契约 §3.20）；`queue` 一律对象数组形态；缩进 2 空格；无尾随空格；
  3. **顺序语义**：`available` 内后端的**声明顺序即优先级**（§2.8）⇒ 整理时**不得重排**已有顺序（新增 43284 放在哪里见 U11）；
  4. **注释清理**：删除过时注释（「token 列表」「待迁移」一类历史说明）；保留必要说明；
  5. **无未使用键**：不出现已删除 owner（`common` / `platform` / `selection`）与已注释段；
  6. **同源**：8 个 label 相关文件名/字段一律来自 `lkm-kmi-manifest.tsv`（不手抄）；
  7. **逐资产对拍**：整理前后跑「归一化计划等价」对拍（沿用 `ProfileLayoutEquivalenceTest` 方式）；差异**只允许**「新增 43284 声明 + 注释 + 顺序」；
- **逐文件清单（初查，实现批次以 `ls` 复核到 68 个 `.conf`）**：

```text
assets/profile/  （改名后）
  index.conf                       登记 8 份 general（§2.5）；backends 段复核
  4 个 template（5.15/6.1/6.6/6.12） 并入 general（§2.5）后是否保留为骨架参考 ⇒ 见 U4b
  各 release profile（约 58 个）      逐份：补 43284 声明；43499 只留实测通过项；按上面 7 条
  共享片段（execution-*/credential-6x/kernelsnitch-6x）  不含选择面，通常无需改（逐份确认）
```

- **代价**：58+ 份逐一改动，量大但机械；**风险**：误改几何/凭据 ⇒ 必须用逐资产对拍兜住。
### 2.5 (4)(0.2-1) general profile：**8 份**，每份对应一个 LKM label

- **形态**：**每 label 一份**（裁决 0.2-1：不要并入一份）；**命名建议** `general-<kmi-label>.conf`（例 `general-android13-5.15.conf`、`general-android15-6.6.conf`）；**label 词表来自 `lkm-kmi-manifest.tsv`（同源、不手抄）**；
- **登记方式（待确认 U4b）**：候选 ① 复用 `profiles = [ { release, file } ]`，`release` 用保留名 `general-<label>` ② 新增 `generals = [ … ]` 列表 ③ 双列表并存；**建议 ①**（改动最小、解析路径复用）；
- **43499 空模版段**：**只有内核族 ∈ {5.15, 6.1, 6.6, 6.12} 的 general 才写**（裁决 0.2-1 后半句）；族由 label 派生（§1.5）；模版内容 = 现有四份 template 的**空骨架**（几何/凭据/offset 保持空或 null，表示未测）；**其余 4 个 label（5.10×2、6.18）不写 43499 模版**；
- **默认 backend = `cve_2026_43284`**，且**必须放在 `available` 的第一项**（满足 U1 默认值语义，§2.8；**U11 已裁决 2026-10-06：43284 声明放第一项**）；
- **43499 的可用性声明一律注释掉**（首轮 (4) 要求仍有效）——**精确写法**：

```hocon
available {
  cve_2026_43284 { queue = [ { step = "pagecache_write" } ] }   # 第一项 = 默认；顺序即优先级
  # cve_2026_43499 { route = "multicast_waiter" }               # 按用户指令注释：general 不声明 43499 可用
}
```

- **注释后的语义（必须写清）**：`available` **不是** native 的执行输入（native 只按 wire 的 `backend.<id>` 与 `route`/`queue` 绑定，契约 §3.20）⇒ **「注释掉」= 「未声明」**，**不是** `available = false`；**风险**：若 App 仍发射 43499 组合，native 仍会执行 ⇒ **必须由 App 侧门禁（§2.3 + §2.9）兜住**；
- **label ↔ 族映射与冲突处理**：映射源 = `lkm-kmi-manifest.tsv`（§1.5）；**同族多 label**（5.10×2、5.15×2）⇒ **一对一，不合并**；「同族只留一份」的诉求记为后续议题（**不在本批**）；
- **代价**：8 份内容高度相似 ⇒ **必须由生成任务从 manifest + 单一模板产出**，否则手抄必漂移；**建议**：Gradle 任务生成到 `assets/profile/`（构建逻辑一律进 Gradle KTS）；
### 2.6 (5) general 回退（**与组合维度的 fallback 是两件事**）

- **触发**：**精确匹配失败**（`index.conf` 无该 release）⇒ 取**该设备 label 对应的 general**（label 由设备 release 派生，见 §2.9 可推断清单）；**该 label 无 general** ⇒ 仍不可运行（红色）；
- **位置（Kotlin）**：`AndroidProfileConfigController.load()`（`:95`）→ `resolveCurrent()`（`:742`）→ `resolve()`（`:752`）；在**最外层**加「精确匹配失败 ⇒ 用对应 general 再解析一次」；**native 不参与**（§1.4）；
- **例外（仍拒绝）**：① 连 general 也缺失/解析失败；② **选择的后端/路径的必须参数缺失**（§2.9，红色）；
- **验证口径（真机）**：构造未登记的 release（或临时移走该 release 的 conf）⇒ 期望**加载对应 general**、UI 默认项 = `available` 第一项（43284）、43284 链跑通；冷机 + 固定 CPU 对 + KernelSU 未加载；日志落 `Download/ghostlock-debug-log/<时间>/`，核对 `profile_resolved` 指向 general；
- **⚠ 区分两类 fallback（用户裁决要求，必须写清）**：
**「未验证」标注的分支规则（U7 已裁决 2026-10-06；本规则是 Lead 依该裁决推出的最小解读，用户可推翻）**：
- **不加二次确认**（理由：43284 非常通用）；
- **默认 general → 43284 路径：不加任何标注**；
- **仅当** general 路径实际落到**非 43284** 后端（例：用户自行放开被注释的 43499 声明）时 ⇒ **保留一个轻量标注**（`execution_combo_unverified_hint`，一行提示，不阻断）；
- **可复核判据**：`selectedBackend != cve_2026_43284` 且 `profileKind == general` ⇒ 显示标注；其余情况不显示。
  - **保留**：**kernel-release 维度的 general 回退** —— 用户首轮 (5) 明确要求；
  - **退役**：**组合/选择维度的隐式 fallback** —— 用户裁决「以前的 fallback 已不存在，由 available 手动选择代替」⇒ 逐条清单见 §2.10；
  - **如有歧义**（例如 `activeBuiltinRelease()` 这类「隐式换了一个 release」），列为**待确认 U12**。
**U12 事实补齐（2026-10-06，读代码结论，不猜）**：
- **定义**：`AndroidProfileConfigController.kt:585` —— `override fun activeBuiltinRelease(): String? = forcedBuiltinRelease ?: preferences.getString(PrefBuiltinRelease, null)?.takeIf { it.isNotEmpty() }`；
- **语义（结论）**：返回值 = **用户显式指定/持久化的「内置 profile release」**（`forcedBuiltinRelease` 或偏好键 `PrefBuiltinRelease`），**不是随机回落**；**无设置时为 null**；
- **用法**：`AndroidProfileConfigController.kt:748` 的 `val profileRelease = activeBuiltinRelease() ?: deviceRelease` ⇒ **当用户显式设了内置 release 时，App 会用那个 release 的 profile 而不是设备 release 的** ⇒ **这确实会让 App 用「别的 release 的 profile」**（**但仅在用户显式设置时**）；
- **暴露给 UI**：`GhostlockViewModel.kt:227` / `:1213` 以 `activeBuiltinProfile` 形式暴露（供界面显示/切换）；
- **归类（结论）**：**它属于「显式选择」而非「隐式 fallback」** ⇒ 依用户「以前 fallback 已不存在、由 available 手动选择代替」的裁决，**不建议删除**；但按「状态必须与真实一致」的纪律，**必须显式化**：
  - **做法①：UI 明示 profile 来源**（顶部/配置页显示「当前 profile：设备 release / 内置 release X / general-<label>」）；
  - **做法②：与 general 回退合并成一条可见的解析链**（精确匹配 → 用户显式指定 → general-<label>），并把「最终用了哪一个」写进运行日志（`profile_resolved` 已记录同类信息，`:545`）；
- **U12 · 已裁决（2026-10-06）：与现有配置加载机制合并** —— 用户原话：「**U12 和现有配置加载机制合并吧**，一般用户自行提取的都是**精确匹配内核**的，所以**未检测到匹配的配置就加载 general**（**匹配机制为同时在内置，和用户导入配置中匹配，其中优先匹配用户导入的最新配置**）」。
  ⇒ **不再保留 `activeBuiltinRelease()` 作为独立的隐式替换路径**，而是**并入「profile 解析链」**（一等规格见 **§11**）：`forcedBuiltinRelease` 类偏好降级为链上的**「①′ 显式指定」**（优先级最高、**必须在 UI 可见**，不允许静默）；
  **沿革（不删历史）**：曾提出「显式化（做法①UI 显示来源 / 做法②与 general 回退合并）」⇒ 用户裁决**合并**（即采用做法②的方向，并进一步把「用户导入优先」写成链的显式规则）。

### 2.7 (6) 改名 `kernel_profile` → `profile`

- **逐条清单**：

```text
1. 资产目录        app/src/main/assets/profile/  → app/src/main/assets/profile/
2. 索引自引用      index.conf 头注释与 profiles[].file 路径语义
3. Kotlin/测试     app/src/test/.../*Test.kt 的 profile/… （6+ 处）
4. Gradle 任务     exportProfiles → exportProfiles（或保留别名，见 U9）
5. 文档            docs/profile/**、docs/development/**、AGENTS.md、README(_ZH).md
6. v1 夹具        profile-legacy/ 是否随之改名（见 U8）
```

- **向后兼容**：项目未发布 ⇒ **建议不留旧名别名**（避免长期双真相），并写明影响面（旧路径/任务名/本地笔记失效，机械替换即可）。
### 2.8 (补充裁决) `available` 的**声明顺序 = 优先级**，默认值 = 第一项

**用户原话**：「U1 未选择时的默认值为 available 声明的第一项，同时 available 声明隐含了优先级的含义，并且确认以前的 fallback 已不存在，由 available 手动选择代替」。

**规则**：
- **默认值**：子菜单未选择时 ⇒ 取已加载 profile 的 `available` **第一项**（按声明顺序）；
- **顺序即语义**：`available` 内条目的先后 = **优先级**（**不是排版**）⇒ 文档、测试、编辑器都不许重排；
- **适用范围**：本条**只约束 Kotlin 选择/呈现**；`available` **不是** native 执行输入（§2.5 已核实）⇒ **顺序契约只约束 Kotlin** ✓。

**顺序保真核实（实现前提，已查到的证据）**：

| 环节 | 现状 | 证据 | 结论 |
|---|---|---|---|
| HOCON 解析 | 保序 | `profile-core/src/main/kotlin/com/ghostlock/app/data/HoconSupport.kt:11`（注释写明模型是 `LinkedHashMap / ArrayList / scalars`）与 `:47`（`linkedMapOf<String, Any?>()`） | ✓ 解析不打乱顺序 |
| 值模型 | 保序 | `profile-core/src/main/kotlin/com/ghostlock/app/data/ValueModel.kt:5`（同一句：`LinkedHashMap / ArrayList / scalars`） | ✓ |
| 规范化 | 需实测 | `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:914` 用 `sortedMapOf`，但**只出现在 `flatten()`（等价对拍用）**，不是生产 canonical 路径 | ⚠ **实现批次必须实测 canonical 输出仍保序**；`flatten` 只用于对拍（有序无关） |
| 合并/覆盖 | 需实测 | `AndroidProfileConfigController.resolve()`（`:752`）与覆盖读取（`readAdvancedOverride`）路径 | ⚠ 待实测：merge 是否按 LinkedHashMap 保序拼接 |
| 缓存 | 需实测 | `AndroidProfileConfigController` 的 `cache(...)` | ⚠ 待实测：缓存是否原样回放顺序 |

**守卫方案（建议）**：
- 加一条**单测**：构造 `available` 两项（43284 在前、43499 在后）⇒ 断言解析结果、merge 结果、缓存回放后的**键顺序完全一致**；
- 若某环节不保序 ⇒ **优先修该环节**（改为 `LinkedHashMap` 保序拼接）；
- **替代方案（仅当修不动时，需用户裁决，选项 + 代价）**：
  - **选项 A：显式 `priority` 字段**（如 `cve_2026_43284 { priority = 0 }`）——代价：语法变复杂、与「顺序即优先级」的直觉不一致；收益：与实现无关；
  - **选项 B：恢复「有序列表」语法**（把 `available` 写成数组）——⚠ **与 M5 裁决冲突**：M5 刚把 HOCON 列表形态定为「出现即拒」（契约 §3.20）；若走这条路，必须**同批改 M5 的拒绝语义**并说明代价（拒绝规则退化、与 `queue` 的对象数组风格不一致）；**不建议**；
  - **更正（2026-10-06，用户纠错）**：此前把它写成「**与 M5 哲学冲突**」是**不准确**的 —— M5 拒的是 **token 列表**（字符串形态的旧语法糖），其理由是「**队列取代 token**」；而**对象列表**（每项是完整声明）**与 M5 的理念并不冲突**，真实代价只是**要放开 M5 的列表拒绝**（改诊断与测试）⇒ **属实用相邻改动，不是哲学冲突**。详见 §2.8.1 的 A/B/C 决策简报（其中 C = 对象列表）；
  - **选项 C：显式索引键**（如 `first = "cve_2026_43284"`）——代价：多一个键要维护；收益：不依赖解析器行为。

**证伪方案（写进批次门槛）**：
- ① 把 `available` 里**第一项注释掉** ⇒ 默认值必须变成**新的第一项**（不得崩溃、不得回落旧值）；
- ② 把两项**调换顺序** ⇒ 默认选项必须**随之改变**（证明顺序真是语义）；
- 两次都强制重跑（`--rerun-tasks`），记录「造错点 → 输出 → 撤回核验」。

### 2.9 新加载判据（**取代「未精确匹配即拒绝」**）——裁决 0.2-4

**规则原文**：「未精确匹配即拒绝运行是旧规则，新规则为检查选择的后端/路径的必须参数是否填写（不包括可自动推断的），同时配置编辑中可自动推断的项缺失时要明确提示，颜色要黄色而不是红色」。

**① 必须参数（缺失 ⇒ 不可运行，红色）——按 backend/route 列，初稿需确认（U10）**：

```text
共同     : release、available 段、所选 backend 段、queue（对象数组、非空、元素合法）
43499    : route（队列级、必填，契约 §3.20）；该 route/terminal 实际读取的 abi/task_struct/cred/offset 字段
43284    : queue 中的步骤序列；模块/载体相关路径（若策略要求显式提供，见「可推断」）
terminal : 所选 terminal 的必须输入（umh 的 argv 形状、root_child 的脚本路径来源）
```

**原则**：只把「**无法从 release/设备事实推导、且运行期真正读取**」的字段列为必须；清单由实现批次从 `profile-manifest-v3.tsv` 的 `required` 列 + 该路径实际读取点**逐条导出**，**不手抄**。

**② 可自动推断（缺失 ⇒ 可运行，黄色提示）——初稿需确认（U10）**：

```text
kmi label      : 由设备内核 release 派生（major.minor ⇒ 族；label 由 lkm-kmi-manifest.tsv 匹配）
43284 kmi      : 由 release 派生（major*1000+minor）
lkm/carrier 路径: 统一在 GhostLock 内部目录解析（契约 §3.16：profile 不承载）
缺省调参        : late_load_args / selinux_exec_context / module_poll_* / wait_timeout_ms 的 literal 默认值
geometry 派生物 : 可由 extractor/设备事实推导的偏移（缺失按 null=未配置处理）
```

**③ 执行层**：**App 侧（Kotlin）**——`AndroidProfileConfigController`（校验）与编辑器 UI（提示）；**native 不参与**（§1.4）⇒ **不新增 native 逻辑**；
**③-补 · U10 已裁决（2026-10-06，按 Lead 建议）**：
- **可推断（黄色）**：**由 release 可派生**的项 —— `kmi label`、`kernel_major`、`kernel_minor`、`release` 自身；
- **必须（红色）**：**backend/route 真正要喂给内核的 ABI 常量**（结构体偏移、符号偏移、凭据几何等运行期实际读取的常量）；
- 因此 §2.9-①②的初稿清单按此口径收敛（上面的「共同/43499/43284/terminal」条目只保留**ABI 常量类**为必须；`release` 类的派生项移入可推断）。

**⑦ 新增校验点：加载后、攻击前（U2 裁决的落地形态）**
- **时机**：**profile 加载完成之后、发起攻击之前**（对应 `AndroidGhostlockRepository` 的运行前置检查处，现为 `:549` 的拦截点）；
- **谁执行**：**App（Kotlin）**——`AndroidProfileConfigController` 提供判据结果，运行前置检查负责**汇总并报错**；
- **错误项如何呈现**：**逐项列出「错误项」**（字段路径 + 原因 + 红色），**阻断运行**；可推断项的缺失**不列入错误项**（黄色提示，见 `profile_check_derivable_missing`）；
- **必须与「声明为准」一致**：报错基于**profile 声明 + 必须参数**，**不是**基于「代码是否已接线」；
- **证伪**：① 在一个可用 profile 里**删掉一个 ABI 必须常量** ⇒ 加载后、攻击前**必须报红色错误项并拒绝运行**；② 只删一个可推断项（如 `kmi`）⇒ **不报红**、可运行、编辑器黄色提示。
**④ 用户可见行为**：必须参数缺失 ⇒ **红色**（阻断，`profile_check_missing_required`）；可推断项缺失 ⇒ **黄色**（不阻断，`profile_check_derivable_missing`）；「该后端/路径必须参数不完整」⇒ 对应行**不可选**并红色（`profile_check_backend_unrunnable`）；
**⑤ 旧规则退役清单（逐条 file:line，实现批次全部改口）**：

```text
AGENTS.md:5                                   内核按精确 uname -r 匹配 HOCON profile，未匹配即拒绝运行
README_ZH.md:7（README.md 英文对应段同）      按精确 uname -r 匹配，未匹配的内核直接拒绝运行
docs/profile/PROFILE_TEMPLATE.conf:45  release … 必填；内核按此精确匹配，不匹配即拒绝运行
docs/profile/PROFILE_SCHEMA.md/_ZH.md  release 语义段（同类表述，逐条复核）
docs/profile/README.md/_ZH.md          如何支持一款新内核段（同上）
index.conf                                     profiles 列表语义（唯一来源表述按新判据改写）
代码 AndroidProfileConfigController.kt:101      hasProfile=false（改为必须参数缺失才阻断）
代码 AndroidGhostlockRepository.kt:549         运行前拦截（同上，改为判据结果）
代码 GhostlockViewModel.kt:218 / :1650         executionHasProfile 语义扩展
```

**⑥ 证伪方案**：删掉一个**必须参数**（如 43499 的 `route`）⇒ **必须拒绝运行且报红**；删掉一个**可推断参数**（如 43284 的 `kmi`/路径）⇒ **必须仍可运行**且编辑器显示**黄色**提示；两次强制重跑并记录三件套。

### 2.10 隐式 fallback 退役清单（**组合/选择维度**；与 §2.6 的 general 回退区分） **界定（2026-10-06）**：本清单是**代码里的隐式回退**（行为），与 **v2 的 `fallback.route.*` 配置语法**（归 `LegacyProfileConverter`，见 §2.13）**不是一回事**，不得混谈。

| # | 位置 | 现状 | 拟处理 |
|---|---|---|---|
| 1 | `GhostlockViewModel.kt:1289` | `?: CombinationCatalog.availableForBackend(kind).firstOrNull()` —— 候选里找不到就**隐式取该后端第一个可用组合** | **删除**：改为**只按 profile 的 `available` 顺序**取（第一项 = 默认，§2.8）；找不到即不可选 + 红色 |
| 2 | `GhostlockViewModel.kt:1286` | `specs.firstOrNull { it.available && it.backend == kind && it.steps == current.steps && it.route == targetRoute }` —— 按当前状态**隐式匹配** | **改为显式**：目标组合由声明列表决定，不再按 steps/route 猜 |
| 3 | `GhostlockViewModel.kt:1263` | `specs.firstOrNull { … }`（组合选择处的同类匹配） | 同上 |
| 4 | `GhostlockViewModel.kt:1283` | `if (!kind.available) return` —— 用**编译期目录**判可用性 | **改为**读 profile 声明（§2.3），目录只作「已接线」交集 |
| 5 | `CombinationPresentation.kt:26` | `enabled = it.available`（目录驱动） | 同上（UI 侧改由声明驱动） |
| 6 | `AndroidProfileConfigController.kt:748` | `activeBuiltinRelease() ?: deviceRelease` —— **隐式**把「当前内置 release」当成 profile release | **待确认 U12**：若「active builtin」是用户显式选择 ⇒ 保留；若是隐式回落 ⇒ 删除（改用 general 回退，§2.6） |

**保留（不退役）**：kernel-release 维度的 **general 回退**（§2.6）—— 用户明确要求。
## 3 分批落地顺序与门槛（按新裁决重排）

| 批 | 内容 | 门槛 / 证伪 |
|---|---|---|
| ① 改名 | `kernel_profile*` → `profile*`（§2.7） | `:app:assembleDebug` + `:app:testDebugUnitTest`（EXIT=0，0 告警）；纯机械 ⇒ 无真机 |
| ② general 8 份 + 内置整理 + 声明注释 | §2.5（8 份：四族含空 43499 段、默认 43284 **放第一项**、注释 43499 声明）+ §2.4（58+ 份整理并加 43284） | 资产对拍（归一化等价；差异只允许「新增 43284 声明 + 注释 + 顺序」）+ 单测；**无真机** |
| ③ available 顺序保真（§2.8） | 单测覆盖解析/merge/缓存三环节；不保序则修 | 单测 + **顺序证伪两条**（注释掉第一项 ⇒ 默认变新第一项；调换两项 ⇒ 默认随之改变） |
| ④ 新加载判据（§2.9）+ **加载后/攻击前校验点（§2.9-⑦）** + fallback 退役（§2.10） | 必须=ABI 常量（红）/ 可推断=release 派生（黄）清单 + 编辑器红/黄 + **运行前汇总错误项** + 旧规则改口 + 隐式 fallback 删除 | 单测 + **三条证伪**（删 ABI 必须常量 ⇒ 红且拒绝；删 release 派生项 ⇒ 黄且可运行；乱写声明 ⇒ 加载后攻击前报错项）+ **真机**：未匹配 release ⇒ 挂 general 并跑通 43284 |
| ⑤ UI 子菜单 + 本地化（§2.1/§2.2） | 新页面 + `execution_combo_*` + `profile_check_*` | 单测 + **三张截图**（入口 / 列表 / 不可用态） |
| ⑥ 可用性判据接线（§2.3） | profile → UI 通路 + 门禁 | 单测 + **证伪**（注释掉声明 ⇒ 该行不可选）+ 真机复核 a301so 只能选 multicast（+43284，默认按声明第一项） |

**顺序取舍（④ 与 ⑤/⑥ 的先后）**：
- **选项 A（本稿建议）**：② → ③ → ④ → ⑤ → ⑥。理由：先立「默认值/优先级」与「能不能跑」的地基，再改呈现；
- **选项 B**：② → ③ → ⑤ → ⑥ → ④。理由：用户先看到子菜单；代价：UI 会短暂基于旧可用性语义与旧拦截规则。

## 4 待确认点

### 4.1 已裁决（2026-10-06，保留原文与沿革，不删历史）

- **U1 · 已裁决**：子菜单未选择时 ⇒ **默认值 = `available` 声明的第一项**；且 **`available` 的顺序隐含优先级**（顺序是语义，不是排版），**以前的 fallback 已不存在，由 available 手动选择代替** ⇒ 落点见 §2.8（含顺序保真核实与守卫）；
- **U3 · 已裁决**：a301so 的 43499 **只声明 mcast** + **同时加入 43284 声明**；**其他内置 profile 也一并整理并加入默认 43284 声明**（理由：**43284 声明更简单易用**）⇒ 落点见 §2.4；
- **U4 · 已裁决**：**不做一份 general，改做 8 份**（每个 lkm 库一份）；**非 {5.15, 6.1, 6.6, 6.12} 的 general 不写 43499 模版** ⇒ 落点见 §2.5（登记方式仍有一子问题，见 4.2-U4b）；
- **U5 · 已裁决**：首轮「42384」是**口误**，实指 **`cve_2026_43284`** ⇒ general 默认 backend = `cve_2026_43284`；
- **U6 · 已被取代（沿革保留）**：原问「general 是否需要适用范围声明（内核族/最低版本）」——**被裁决 0.2-4 的新加载判据取代**：不再用族/版本声明做门槛，改为**逐字段的必须/可推断判据**（§2.9）。取代理由：逐字段判据更精确，且能把「可推断」与「真缺失」分开提示（红/黄）。
- **U2 · 已裁决（2026-10-06）**：**以 profile 声明为准**——「代码里是否已接线」**不代表本机支持这种路线**；若有人乱写 profile ⇒ **在「加载后、攻击前」报出错误项**（红色）。落点：§2.3（判据）+ **§2.9-⑦（新校验点：位置/时机/谁执行/错误项呈现）**；
- **U7 · 已裁决（2026-10-06）**：**无需二次确认**（43284 非常通用）；**general → 43284 路径不加标注**；**仅当** general 实际落到**非 43284** 后端时保留**轻量标注** ⇒ 落点见 §2.6 的**分支规则**（并注明「最小解读，用户可推翻」）；
- **U8 · 已裁决（2026-10-06）**：**不留旧名别名**；**`profile-legacy/` 一并改名**（机械替换）；
- **U9 · 已裁决（2026-10-06）**：Gradle 任务 `exportProfiles` → **`exportProfiles`**，**不留旧名**；
- **U10 · 已裁决（2026-10-06，按 Lead 建议）**：**由 release 可派生的（`kmi label` / `kernel_major` / `kernel_minor` / `release`）归「可推断（黄）」**；**backend/route 真正要喂给内核的 ABI 常量归「必须（红）」** ⇒ 落点见 §2.9-③补；
- **U11 · 已裁决（2026-10-06）**：**新增的 43284 声明放 `available` 第一项**（与 general 一致）⇒ 落点见 §2.5/§2.4；

### 4.2 仍待确认（不替用户决定；保留选项 + 建议）

- **U4b**：8 份 general 在 `index.conf` 的**登记方式**（① 复用 `profiles` 的保留 release 名 / ② 新增 `generals` 列表 / ③ 双列表）与**是否由 Gradle 生成**？**建议**：① + 生成任务；
- **U12**：`activeBuiltinRelease() ?: deviceRelease`（`AndroidProfileConfigController.kt:748`）属**显式选择**还是**隐式 fallback**？**建议**：若是隐式 ⇒ 删除（改用 §2.6 的 general 回退）；若用户在 UI 显式选过 ⇒ 保留但需在 UI 明示；
- **U13（新增，= §7 的 A/B/C）**：`available` 顺序（优先级）的保障方式选 **A（`priority` 字段）/ B（声明顺序 + 保真守卫）/ C（对象列表）**？**本文不预选**（§7 决策简报 + 对照表）；**建议的决策路径**：先跑 §2.8 的三环节顺序单测，若链路保序 ⇒ **B 代价最小**；若不保序且修不动 ⇒ 再在 A/C 之间取舍。

## 5 明确不做 / 风险

- **不做**：native 侧 release 匹配或回退（§1.4 已核实）；第二份选择状态；「未声明也可选」的宽松模式；**同族多 label 合并成一份 general**（本批不做）；**恢复 HOCON 列表形态**（与 M5 冲突，除非用户另行裁决 §2.8 选项 B）；
- **风险 R1**：8 份 general 让**未测内核**也能跑 ⇒ 与「未过真机不得标 supported」冲突 ⇒ 需 U7 的标注/确认；
- **风险 R2**：`available` 声明与编译期目录**双重真相** ⇒ 判据必须写明并由证伪守住；
- **风险 R3**：改名跨 docs/code **143 处** ⇒ 独立批次 + 机械替换 + 全量测试；
- **风险 R4**：58+ 份 profile 批量「整理」易漂移 ⇒ 逐资产对拍兜住；8 份 general 建议由生成任务产出；
- **风险 R5（已更正 + 更新）**：**顺序保真**未全链路实测（§2.8 表：规范化/merge/缓存三处待实测）⇒ 若不保序且修不动，**在 §7 的 A/B/C 里选**（**更正**：C（对象列表）与 M5 **不是哲学冲突**，只是要放开列表拒绝并同步改诊断/测试；见 §7 对照表）；**A/B/C 仍待用户裁决（U13）**；
- **风险 R6（新增）**：**新校验点「加载后、攻击前」若只做 UI 提示而不阻断**，会退化成「提示了但照样跑」⇒ 必须**阻断运行**，并由 §2.9-⑦ 的证伪守住。

## 6 影响面与后续

- **契约**：§3.20 需补三条规则——「UI 门禁 = profile 声明 ∩ 设备核实」、「**`available` 顺序 = 优先级、默认 = 第一项**（Kotlin 侧契约）」、「**新加载判据**（必须/可推断）」；
- **profile 校验与编辑器 UI**：校验从「有无 profile」改为「**必须参数是否齐全**」；编辑器需**红/黄两级**（键见 §2.2）与**顺序不可重排**的约束（拖动排序需写回文件顺序）；
- **面向用户文档**：`README(_ZH).md`、`docs/profile/README(_ZH).md`、`PROFILE_SCHEMA(_ZH).md`、`PROFILE_TEMPLATE.conf` 中「未匹配即拒绝」必须改口（§2.9 ⑤），并**新增「顺序即优先级」的说明**；**注意**：面向用户文档的插件内容已折叠为「待开发」，本批**不要**写回插件细节；
- **UML 注记判定**：本批**无结构增删**（UI 呈现 + 校验语义 + 资产内容）⇒ 若 `ProfileConfig` 新增「可用组合集合（**有序**）」字段，则 **Class Kotlin（§3.2）需同批加注**（写明「有序」）；其余图不需要改；
- **面向用户文档（补充）**：新校验点与「声明为准」需在 README(_ZH) 的「如何使用」段写一句（**只写用户可见行为，不写内部实现**，且**不要写回插件细节**）。
- **计划**：进度登记到 `branch-plan.md`（入口行已加，落地后按批勾选）。
## 7 A/B/C 决策简报：`available` 顺序（优先级）的三种保障方式

> 背景：U1 已裁决「**默认值 = `available` 第一项**、**顺序即优先级**」⇒ 必须保证顺序**可靠可读**。本文只列事实与代价，**不替用户选择**（结论区三条并列）。
> **更正记录**：我此前把「对象列表」与 M5 说成哲学冲突，**不准确**（用户纠错）。M5 拒的是 **token 列表**（字符串语法糖），理由是「队列取代 token」；**对象列表与 M5 理念不冲突**，代价只是放开列表拒绝（实用相邻）。

### 7.A 方案 A：显式 `priority` 字段

**① 用户面语法**

```hocon
available {
  cve_2026_43284 { priority = 0, queue = [ { step = "pagecache_write" } ] }
  cve_2026_43499 { priority = 1, route = "multicast_waiter", queue = [ { step = "w1" } ] }
}
```

**② 要改的文件**
- `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt`：`validateAvailable`（`:420`）与 `validateAvailableSelection`（`:453`）新增 `priority` 键校验（整数、唯一、可缺省 ⇒ 缺省排最后）；
- `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:435/:438`：**不动**（列表拒绝保持）；
- 契约 §3.20 + `docs/profile/PROFILE_SCHEMA(_ZH).md` + `PROFILE_TEMPLATE.conf`：新增 `priority` 说明；
- 8 份 general 与 58+ 份 profile：逐个补 `priority`（或依赖缺省规则，见风险）。

**③ M5 的拒绝与测试**：**不动**（无列表、无 token ⇒ 与 M5 无交集）。
**④ 迁移与兼容**：未发布 ⇒ 无兼容负担；但**所有已写 `available` 的资产都要补键**（或声明「缺省 = 最后」）。
**⑤ 证伪方案**：把 `priority` 改成非法值（负数/重复/非整数）⇒ 必须拒绝并给具名诊断；把两项 `priority` 对调 ⇒ 默认项必须随之改变。
**⑥ 风险**：语法变复杂（用户面多一个概念）；「顺序」与「priority」可能不一致（文件顺序 A,B 而 priority 让 B 在前）⇒ 需在编辑器里明示或强制重排。

### 7.B 方案 B：依赖声明顺序 + 保真守卫（**零语法改动**）

**① 用户面语法**：与今天完全一致（顺序即优先级）：

```hocon
available {
  cve_2026_43284 { queue = [ { step = "pagecache_write" } ] }   # 第一项 = 默认
  cve_2026_43499 { route = "multicast_waiter" }                 # 第二项 = 次选
}
```

**② 要改的文件**
- `profile-core/src/main/kotlin/com/ghostlock/app/data/HoconSupport.kt:11/:47`：**不改**（已用 `linkedMapOf` 保序）；
- `profile-core/src/main/kotlin/com/ghostlock/app/data/ValueModel.kt:5`：**不改**（同一句已写明模型保序）；
- `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:914`：**只用于 `flatten()`（等价对拍）**，需**确认**它不进入生产 canonical 路径；若进入 ⇒ 改为保序；
- `AndroidProfileConfigController`：`resolve()`（`:752`）与 `cache(...)` 需**加顺序单测**（merge/缓存两环节待实测，§2.8 表）；
- 契约 §3.20：写明「**Kotlin 侧契约：`available` 顺序 = 优先级**」（native 不受影响）；
- 文档：`PROFILE_SCHEMA(_ZH).md`、`PROFILE_TEMPLATE.conf`、8 份 general 的注释里写明「顺序即优先级，勿重排」。

**③ M5 的拒绝与测试**：**不动**。
**④ 迁移与兼容**：**零资产改动**（现有文件顺序即语义）；无兼容负担。
**⑤ 证伪方案**：① 注释掉第一项 ⇒ 默认变新第一项；② 调换两项 ⇒ 默认随之改变；③ **解析→merge→缓存三环节顺序单测**（§2.8 守卫）。
**⑥ 风险**：依赖解析器/序列化链的保序性（**已核实解析层保序**，merge/缓存**待实测**）；若某环节排序 ⇒ 静默改变优先级（**高危**）；编辑器若支持拖动排序，必须写回文件顺序。

### 7.C 方案 C：`available` 用**对象列表**（每项完整声明）

**① 用户面语法**

```hocon
available = [
  { backend = "cve_2026_43284", queue = [ { step = "pagecache_write" } ] },
  { backend = "cve_2026_43499", route = "multicast_waiter", queue = [ { step = "w1" } ] }
]
```

**② 要改的文件**
- `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:420`（`validateAvailable`）：接受**对象列表**分支；`:453`（`validateAvailableSelection`）：按元素 `backend` 字段校验；
- `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:435/:438`：这两条**列表拒绝**要**放宽/改写**（区分「token 列表」与「对象列表」两种形态）；
- `profile-core/src/test/kotlin/com/ghostlock/app/data/ProfileLayoutAvailableTest.kt:31/:36`：**必须改**（「空 token 列表」用例的期望要区分形态）；
- 8 份 general 与 58+ 份 profile：`available` 段整体改写为列表形态；
- `index.conf` 的 `generals` 登记（U4b）与文档（契约 §3.20、PROFILE_SCHEMA(_ZH)、PROFILE_TEMPLATE.conf）同步。

**③ M5 的拒绝与测试（逐条）**
- **Kotlin 拒绝 1**：`ProfileLayout.kt:435`「`the token-list form was removed in M5; declare route+queue`」⇒ **保留**（只针对**字符串/token 列表**），新增「对象列表」分支**不受影响**；
- **Kotlin 拒绝 2**：`ProfileLayout.kt:438`「`empty token list is not a selection; declare route+queue`」⇒ 需**区分**：空**对象**列表 ⇒ 新诊断（空声明不是选择）；空 **token** 列表 ⇒ 保持拒绝；
- **Kotlin 测试**：`ProfileLayoutAvailableTest.kt:31/:36` 的「空列表」用例需**按形态拆分**（token 列表仍拒；对象列表新增正例）；
- **native 侧**：`token-form-removed`（`src/core/tests/combination_token_test.cpp:118/:122/:139`）针对**取值**而非 `available` 形态 ⇒ **不动**（`available` 不是 native 执行输入，§1.4/§2.5）；
- **契约 §3.20 的「列表形态出现即拒」表述**：需按「**区分 token 列表 / 对象列表**」改写（**这是本条的主要文档代价**）。

**④ 迁移与兼容**：未发布 ⇒ 无兼容负担；但**资产需整体改写**（58+ 份 + 8 份 general）⇒ 与 §2.4 的「整理」批次可合并，但**改动面显著大于 A/B**。
**⑤ 证伪方案**：① 空对象列表 ⇒ 新诊断且拒绝；② 元素缺 `backend` ⇒ 拒绝；③ 调换两个元素 ⇒ 默认项随之改变；④ token 列表（旧语法糖）⇒ **仍然拒绝**（证明 M5 未被放宽错）。
**⑥ 风险**：改动面最大（资产业已批量迁移过一次，M5 刚稳定）；需同步改 M5 的拒绝语义与测试（**若改不彻底会造成「有的列表被拒、有的被收」的认知混乱**）；与 `queue` 的对象数组风格**反而更一致**（可读性收益）。

### 7.4 代价 / 风险对照表（**不替你选择**）

| 维度 | A：priority 字段 | B：声明顺序 + 守卫 | C：对象列表 |
|---|---|---|---|
| 用户面语法改动 | 中（每项多一个键） | **无** | 大（`available` 段整体换形态） |
| 需要改的代码点 | `ProfileLayout.kt:420/:453` | `AndroidProfileConfigController.kt:752` + `:914` 复核 | `ProfileLayout.kt:420/:435/:438/:453` |
| 需改 M5 拒绝/测试 | 否 | 否 | **是**（`:435/:438` + `ProfileLayoutAvailableTest.kt:31/:36`；native 不动） |
| 资产改动量 | 58+8 份补键（或依赖缺省） | **0** | 58+8 份整体改写 |
| 顺序可靠性 | 高（显式数值） | **依赖链路保序**（解析已核实；merge/缓存待实测） | 高（列表天然有序） |
| 与 M5 的关系 | 无交集 | 无交集 | **相邻实用改动**（非哲学冲突） |
| 主要风险 | 概念变复杂；priority 与文件顺序可能不一致 | 某环节排序 ⇒ **静默改优先级** | 改动面最大；M5 语义需同步改透 |
| 证伪要点 | 非法/重复 priority 拒绝；对调 priority 默认变 | 注释首项/调换顺序 ⇒ 默认变；三环节顺序单测 | 空列表/缺 backend 拒绝；对调元素默认变；token 列表仍拒 |

**结论区（2026-10-06 用户裁决：选 A）**：**优先级机制 = A（显式 `priority`，或等价显式键）** ✓
- **落点**：`profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:420`（`validateAvailable`）与 `:453`（`validateAvailableSelection`）新增 `priority` 键校验（整数、唯一、可缺省 ⇒ 缺省排最后）；
- **资产改动**：**58+ 份 release profile 与 8 份 general 逐份补 `priority`**（或依赖「缺省排最后」规则，但**建议显式写出**，便于阅读）；
- **语义变化**：**文件顺序不再承担语义** ⇒ 编辑器可自由排序；`available` 的读取按 `priority` 升序（最小 = 默认 = 第一项），与 U1 的「默认值 = 第一项」等价（第一项 = priority 最小项）；
- **不再需要的守卫**：原 R5 设想的「解析→merge→缓存三环节保序单测」**在 A 下不再承担优先级语义** ⇒ 改为：**断言 `priority` 生效**（priority 最小项 = 默认项）+ **证伪**（修改 `priority` ⇒ 默认值随之改变）；若链路确实打乱了 map 顺序，**不影响**默认值判定（因为按 priority 排序），但**仍建议**保留一条「顺序不影响结果」的回归断言（避免有人误以为顺序有语义）；
- **`R5-ORDER-01`（`@Ignore` 的三条保序断言）改写**：从「必须保序」改为「**顺序无关于语义**」——三条断言改为：① 打乱文件顺序后默认项**不变**（因 priority 决定）② 调换 `priority` 后默认项**改变** ③ 缺省 `priority` 的项排在所有显式项之后；
- **对 M5 的关系**：**不涉及**（无列表形态）✓；
- **B/C 的沿革保留**：B（依赖顺序）与 C（对象列表）**不再采用**，但**保留在本文作为沿革与备选**（若将来 `priority` 方案因故撤回，可直接复用本节的代价/风险分析）。

## 8 变更记录

- **v1（2026-10-06）**：初稿（172 行），8 条设计 + U1–U9 待确认；
- **v2（2026-10-06）**：并入 4 条最新裁决（8 份 general / U5 口误 / 全部内置 profile 加 43284 / 新加载判据）；新增 §1.5、§2.8、§2.9、§2.10；U3/U4/U5 标已裁决、U6 标被取代；
- **v3（2026-10-06）**：并入 U2/U7/U8/U9/U10/U11 裁决（以声明为准 + 加载后攻击前校验点；无需二次确认 + 非 43284 才标注；不留旧名别名；`exportProfiles` 改名；必须=ABI 常量/可推断=release 派生；43284 放第一项）；**更正 M5 冲突说法**；新增 §7 A/B/C 决策简报与对照表；U12 与 A/B/C 仍待确认。
- **v4（2026-10-06）**：优先级机制**裁决为 A（显式 `priority`）**；新增 §2.1.1（三级树 + 最小充分语法）、§2.1.2（菜单生成策略对照表，采用数据驱动 + fail-visible）、§2.11（`route` 子层扁平化，两种读法待确认）、§2.12（requirements 文件夹）、§2.13（旧 fallback → 显式 `available` 路径）、§2.6 的 **U12 事实补齐**；新增待确认 **U14–U18**。
- **v5（2026-10-06）**：**新需求 1（扁平化 `backend.<id>.route.<route>.*` 子层）已撤回**（用户裁决：与既有 route 扩展节约定冲突，「现有的也挺好」）⇒ §2.11 标「已撤回」、勘察结果**保留为事实记录但不据此改动**、**U14 标已撤回**；其余新需求（§2.12 requirements 文件夹 / §2.13 旧 fallback 注册 / §2.1.1 树状 UI 与简化写法）与 **A（显式 `priority`）**、**U12 事实核实** 照旧推进。
- **v6（2026-10-06）**：**U12 裁决 = 与现有配置加载机制合并** ⇒ §2.6 结论改写为「并入解析链」（`forcedBuiltinRelease` 降级为链上的 ①′ 显式指定、**必须 UI 可见**；沿革保留「显式化两做法 ⇒ 用户选合并」）；新增 **§11 profile 解析链（一等规格）**：链序 = **①′ 显式指定 → ① 用户导入精确匹配（取最新导入）→ ② 内置精确匹配 → ③ `general-<label>`**、伪码、判据（来源可见性 / 与 U7 一致）、**「最新导入」判定选项（建议显式序号/时间戳）**、**四个并列歧义表**、**四条证伪**；新增待确认 **U19–U22**。
- **v7（2026-10-06）**：**U16 重新定性 = v2 legacy** ⇒ §2.13 整节改写（旧 `fallback.route.*` **不进入 v3**：不由 v3 资产承载、不注册 `available` 兄弟项、不参与 `priority`；归属 `LegacyProfileConverter.kt`，已核实其**当前确实处理** fallback 系键 `:457-461`/`:562-570`）；**撤回**原「兄弟项 + 低 priority」提案（沿革保留）；**§2.10 加界定**（代码隐式回退 ≠ v2 配置语法）；新增**验收守卫**（v3 资产不得含 fallback 字样 / 树 UI 无 fallback 节点 / legacy 对照）；新增待确认 **U23**（读法甲=扩展转换器接受 v2（版本政策变更）vs **读法乙=仅定性**（建议））。
- **v8（2026-10-06）**：**U17 裁决 = 扁平注册 + UI 派生树** ⇒ **§2.1.1 整节改写**（配置侧：一条目=一条完整路径、同 backend 可多条；UI 侧：树从扁平派生、层级不入配置）+ **三条承载候选并列**（(a) 对象列表=**建议**，含放开列表拒绝但**继续拒字符串列表**的具名判据，保住 M5 意图；(b) 任意 id 作键=不建议；(c) 嵌变体树=不建议）+ **选定后影响面清单**（`ProfileLayout.kt:420/:435/:438/:453`、M5 诊断与 `ProfileLayoutAvailableTest.kt:31/:36`、golden/fixture 与 76 份资产、`index.conf`；**native 已核实不受影响**：`available` 非执行输入，native 的 `available` 是编译期目录字段）+ **UI 树生成规格**（分组/排序/本地化标签/「未实现」表达与 U2 关系/fail-visible/三条证伪）；新增待确认 **U24（形态选择）/U25（未实现表达）/U26（排序规则）**。

## 9 新增设计条目（2026-10-06 用户新需求）

### 2.11 需求 1：扁平化 `backend.<id>.route.<route>.*` 的子层 —— **⏸ 已撤回（用户裁决 2026-10-06）**

> **撤回裁决（用户原话）**：「1 的话**既然冲突就放弃吧**，**现有的也挺好**」。
> **撤回理由**：它与既有约定 **「route 私有参数放 route 扩展节；只有共享代码会读的才进公共槽」**（AGENTS 代码约定节）**冲突**；用户判断**现状更优** ⇒ **保持现状**。
> **执行边界（必须遵守）**：**不做任何扁平化改动**；本节的**勘察结果保留为事实记录**（对理解现状有用），但**不得据此改动**资产/解析/契约。
> **沿革（不删历史）**：曾提出「把 `backend.<id>.route.<route>.*` 的子层扁平化」⇒ 因与既有约定冲突 ⇒ **用户裁决撤回** ⇒ 现状保留。

**（以下为撤回前的事实勘察，仅供查阅，不作为改动依据）**

**用户原话**：「hocon 配置里 `available` 里已经声明 route 了，但后端配置里还有 route 子类，把后端配置里的 route 子类里的内容扁平化」。

**勘察（现状，file:line）**：
- **资产结构**：`app/src/main/assets/profile/*.conf` 的 `backend.cve_2026_43499` 下有：`abi { task_struct { … } cred { … } }`（例 `5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf:14-40`）、`route { … }`（**62 个资产含 `route {` 块**；例 `5.15-template.conf:54-57` 的 `route { tcp_zerocopy {} select_stack { waiter_shift = null } … }`）、以及 `cred`/`kernel`/`offset`/`execution`；
- **读取侧**：`profile-core/src/main/kotlin/com/ghostlock/app/data/profile/ProfileResolver.kt:45-71` 把扁平名映射到 `route.<name>.<field>`（例 `compact_waiter` → `route.<name>.compact_waiter`、`mcast.*` → `route.<name>.<field>`）⇒ **`route` 层 + route 名层目前是「读取路径约定」的一部分**；
- **既有约定（AGENTS）**：「**route 私有参数放 route 扩展节；只有共享代码会读的才进公共槽**」；
- **native 侧**：`route.<name>.<field>` 由 owner schema 绑定（`profile-manifest-v3.tsv` 的 `backend.cve_2026_43499.route.*` 行）。

**「扁平化」的两种读法（**待确认 U14**）**：
- **读法甲（本稿倾向）**：**保留 `route` 这一层**，只把 `route.<name>` **内部再嵌套的子层**提上来（例：`route.select_stack.waiter_shift` 已是单层，若将来出现 `route.select_stack.adv { x = 1 }` ⇒ 变成 `route.select_stack.x = 1`）；⇒ **不触碰 `route` 层与 `ProfileResolver` 的 `route.<name>.<field>` 约定**，代价最小；
- **读法乙**：**连 `route` 层与 route 名层一起去掉**，把 `route.<name>.<field>` **并入 `backend.<id>` 平铺**（例 `backend.cve_2026_43499.waiter_shift = …`）；⇒ **与既有约定直接冲突**（AGENTS「route 私有参数放 route 扩展节」），且**无法表达「同一字段在不同 route 下取值不同」**（当前 `select_stack.waiter_shift` vs `tcp_zerocopy` 的差异正是靠这一层区分的）⇒ **代价高，需要用户明确裁决**。

**与 AGENTS 约定的冲突处理**：若用户选**读法乙** ⇒ **必须同批改 AGENTS 的那句话**，并写明沿革（原约定为何存在、为何被取代），**不得静默改**；本稿**不替用户决定**。

**跨端影响（逐条）**：
- **native**：owner schema 的 `backend.cve_2026_43499.route.*` 行（`profile-manifest-v3.tsv`）与绑定路径 `glkv3` 解析（`src/core/profile/glkv3.cpp` 的字段收集）——读法甲**零影响**；读法乙**需要新增平铺字段并删除 route 层绑定**；
- **Kotlin**：`ProfileResolver.kt:45-71` 的映射表（读法甲零影响 / 读法乙要删 `route.<name>` 前缀逻辑）；`ProfileLayout` 的归一化与 `flatten()` 对拍（读法乙需重生成 golden）；
- **对拍测试**：`profile-manifest-v3.tsv` 两份副本、`ProfileLayoutEquivalenceTest`、M3 字节清单；
- **资产**：62 个含 `route {` 的 profile + 8 份 general（读法甲基本不动 / 读法乙全量改写）。

**证伪方案**：① 读法甲：在 `route.<name>` 内人为嵌一层子映射 ⇒ 扁平化后**必须**能在 `route.<name>` 下直接读到该键（且 golden/对拍同步更新）；② 读法丙（若采乙）：把 `waiter_shift` 平铺到 `backend.<id>` 后，**同一 backend 下两个 route 的不同取值必须仍可表达**——若无法表达 ⇒ **证明读法乙不可行**（这是一条**可判定的反证**）。

### 2.12 需求 2：`assets` 下新建「执行路径参数需求」文件夹（**待确认命名/粒度**）

**用户原话**：「在 assets 里新建一个文件夹（名字待定）放一些 hocon，描述 `available` 里各执行路径需要哪些参数，并能让 Kotlin 据此检查配置是否有问题」。

- **① 名称候选（选项 + 取舍 + 建议）**：
  - `assets/execution_paths/`——语义直白（路径 → 需求）；代价：与「路径」一词在 UI 的用法略有重复；**建议**；
  - `assets/requirements/`——简短；代价：过于泛化（谁的 requirements？）；
  - `assets/path_spec/`——偏实现术语；代价：面向用户文档里不好解释。
- **② 文件粒度（选项 + 建议）**：每 backend 一份（建议：`43499.conf`、`43284.conf`，与 owner 结构同构）；每 route 一份（更细，但数量随 route 增长）；一份总表（最简，但**冲突合并困难**）⇒ **建议：每 backend 一份**；
- **③ 格式（HOCON，键与 `available` 的路径同构）**：

```hocon
# assets/execution_paths/cve_2026_43499.conf  （示意）
cve_2026_43499 {
  multicast_waiter {
    required   = [ "backend.cve_2026_43499.route.multicast_waiter.<abi 常量…>" ]
    derivable  = [ "backend.cve_2026_43499.kmi", "release" ]
  }
}
```

⇒ **必须与 §2.9 的 U10 裁决一致**：`required`（红）= **真正喂内核的 ABI 常量**；`derivable`（黄）= **由 release 可派生**（kmi label / kernel_major / kernel_minor / release）；
- **④ Kotlin 使用方式（数据驱动）**：读该文件夹 ⇒ 得到「该路径需要哪些键」⇒ 与**已加载 profile 的 canonical 文档**比对 ⇒ 产出**错误项（红）/ 提示项（黄）** ⇒ 交给 §2.9-⑦ 的「加载后、攻击前」校验点；**禁止**把需求清单硬编码进 Kotlin（否则回到双真相）；
- **⑤ 与 native 的关系**：该文件夹是 **App 侧校验元数据**，**不是 wire 的一部分**（native 不读 assets）；理由：native 只认文档，且 ABI 常量的**真实存在性**最终由 native 绑定路径判定（缺字段 ⇒ 绑定失败）⇒ **本稿不做 native 侧读取**（如将来要做，需另一份 L 级设计与真机门禁）；
- **⑥ 证伪两条**：① 从某路径的 `required` 里删掉一个真实需求键（或相反：把某键从 `required` 移到 `derivable`）⇒ 对应配置的错误项**必须**按新分类出现/消失；② 在 profile 里删掉该路径的一个 `required` 键 ⇒ **必须**在「加载后、攻击前」报红并阻断（与 §2.9-⑦ 的证伪联动）。

### 2.13 需求 3：旧 fallback —— **已重新定性为 v2 legacy（用户裁决 2026-10-06）**

**用户原话**：「**U16 的旧 fallback 是 v2 配置，应交给 `LegacyProfileConverter` 处理**，以前的开发中 **v3 从未发布，以最新版 v3 为准**」。

**结论（三条）**：
1. **旧 `fallback.route.*` = v2 时代的配置** ⇒ **不由 v3 资产承载**，**也不在 v3 里新注册「fallback 兄弟路径」** ✗（**撤回**原提案：曾提出「注册为 `available` 兄弟项 + 较低 `priority`」，**沿革保留** ⇒ 用户裁决归 legacy）；
2. **v3 的权威性**：**v3 从未发布** ⇒ ① **无兼容负担**（形状可变，与既有裁决一致）② **以最新 v3 为准** ⇒ **v3 里没有 fallback 这个概念**：`available` 的**路径 + `priority`** 就是全部表达；
3. **legacy 的归属**：任何 v2 形态（含 `fallback.route.*`）⇒ **由 `LegacyProfileConverter.kt` 处理**（项目既有规矩「**只有一个迁移点**」）。**核实（读代码，file:line）**：该转换器**当前确实处理** fallback 系键——`app/src/main/kotlin/com/ghostlock/app/data/LegacyProfileConverter.kt:457-461`（`entry["fallback"].route`，空则移除该子键）与 `:562-570`（`fallback_to` 字符串 ⇒ 写入 `fallback.to`），另有 `:21`/`:91` 的说明注释 ⇒ **归 legacy 域成立** ✓。

**⚠ 口径张力（待确认 U23）**：AGENTS 现行规矩是「读到旧 `schema_version = 1` 时由转换器转为 3；**其余版本值一律拒绝**」⇒ **v2 目前是被拒绝的**；那么「v2 的 fallback 交给转换器处理」有两种读法：
- **读法甲：扩展转换器接受 v2** —— 代价：**版本政策变更**（要改 AGENTS、契约 §3.21 的兼容矩阵、转换器与测试；且需要真实的 v2 输入样本）；收益：v2 用户可平滑迁移；
- **读法乙（Lead 建议）：仅定性**——「fallback 属 legacy 域、**v3 永不含它**」；若真有 v2 profile 进来，**按现行版本政策处理（拒绝）**，但**诊断里点名 fallback**（让用户知道这类配置该怎么迁移）；代价：v2 用户暂时仍需手工处理；收益：**不动版本政策**，与「v3 为准 + 只有一个迁移点」一致；
- ⇒ **两条并列待用户裁决（U23）**。

**连带清理（本次已执行）**：删除原 §2.13 的「注册为 v3 `available` 兄弟项 + 低 priority」提案；**§2.1.1 的树示例与 §2.1.2 的策略表均不含 fallback 概念**；**`priority` 只用于 v3 的 `available` 路径**，不用于表达 fallback。

**与 §2.10 的区分（各一句界定，避免日后混淆）**：
- **§2.10「隐式 fallback 退役清单」**：指**当前代码里的隐式回退**（例如「按后端取第一个可用组合」「把 active builtin 当 profile release」）⇒ 退役目标是**代码行为**；
- **本节（v2 legacy fallback）**：指**旧配置语法** `fallback.route.*` / `fallback_to` ⇒ 归属 **`LegacyProfileConverter`**；**两者不是一回事**，不得互相引用为同一议题。

**证伪 / 验收（作用域限定在 v3 资产与树 UI）**：
1. **v3 资产守卫**：对 `app/src/main/assets/profile/**` 做一次字符串检索，**不得出现 `fallback` 字样**（一条可执行的 grep 守卫即可；命中 ⇒ FAIL）；
2. **树 UI 守卫**：路径树**不得出现 fallback 节点**（用同一份资产喂给 UI 构建逻辑，断言节点集合里无 fallback 系名称）；
3. **legacy 侧对照**：构造一份含 `fallback.route.*` 的 **v2 输入** ⇒ 走 `LegacyProfileConverter` 时**必须被处理/转换**（按 U23 的裁决：甲=转换；乙=按版本政策拒绝且诊断点名 fallback）。
## 10 新增待确认点（U14–U18；一律「选项 + 代价 + 建议」，不替用户决定）

- **U14 · ⏸ 已撤回（用户裁决 2026-10-06）**：`route` 子层扁平化**不做**（理由：与 AGENTS「route 私有参数放 route 扩展节」约定冲突；用户判断**现状更优**）⇒ **保持现状，不据此改动**；沿革与勘察记录见 §2.11；
- **U15（requirements 文件夹命名与粒度）**：名称取 `execution_paths` / `requirements` / `path_spec`？粒度取每 backend 一份 / 每 route 一份 / 一份总表？**建议**：`assets/execution_paths/` + **每 backend 一份**；
- **U16 · 已裁决（2026-10-06）：归 v2 legacy** —— 旧 `fallback.route.*` **不进入 v3**（不由 v3 资产承载、不注册 `available` 兄弟项、不参与 `priority`）⇒ **由 `LegacyProfileConverter.kt` 处理**（`app/src/main/kotlin/com/ghostlock/app/data/LegacyProfileConverter.kt:457-461` / `:562-570`）；原「兄弟项 + 低 priority」提案**撤回**（沿革见 §2.13）；
- **U23（新增，口径张力）**：「v2 的 fallback 交给转换器处理」= **读法甲**（扩展转换器**接受 v2**：版本政策变更，需改 AGENTS + 契约 §3.21 + 转换器与测试 + v2 样本）还是 **读法乙**（**仅定性**：fallback 属 legacy、v3 永不含它；真有 v2 进来**按现行政策拒绝**但在诊断里点名 fallback）？**建议：乙**（不动版本政策，与「v3 为准 + 只有一个迁移点」一致）；
- **U17 · 已裁决（2026-10-06）：配置扁平注册 + UI 从扁平派生树** —— 一个条目 = 一条完整路径（`backend`+`route`+`queue`+`terminal`+`priority`），**同一 backend 可注册多条**；树层级**不写进配置**（§2.1.1）；
- **U24（新增，形态选择）**：扁平注册的**承载形态**取 **(a) 对象列表**（建议；需放开列表拒绝但**继续拒字符串列表**，保住 M5 意图）/ **(b) 任意 id 作键**（不建议：id 形同新 token）/ **(c) 每 backend 嵌变体树**（不建议：即可读性差的那种）？【三候选并列 + 影响面见 §2.1.1】
- **U25（新增，「未实现」节点表达）**：取 ① 条目内 `planned = true`（**建议**）/ ② `index.conf` 的 `usable`（不建议：粒度错层）/ ③ 单独 planned 列表（不建议：第二处真相）？并确认「**未实现项不写进 `available`**，若占位则用 `planned` 显式标注」（与 U2 的关系见 §2.1.1-④）；
- **U26（新增，树排序规则）**：叶子按 **`priority` 升序（建议）**，同级并列用**确定性次序**（步骤序列 + terminal 名的字典序）；一级/二级建议按既有目录顺序（`CombinationCatalog`/词汇 manifest 顺序）；
- **U18（requirements 是否需要 native 侧也读）**：**建议：不做**（App 侧校验元数据；native 只认 wire 文档），如将来需要另开 L 级设计与真机门禁。
- **U19–U22（profile 解析链的子问题）**：见 **§11.6**（最新导入判定 / `forcedBuiltinRelease` 放在链的哪一步 / 用户导入 general 是否享 ① 优先 / 允许不同 release 的显式选择）——本稿**建议**均已给出，**仍待用户确认**。
## 11 profile 解析链（一等规格；2026-10-06 用户裁决）

**用户原话（照录）**：「U12 和现有配置加载机制合并吧，一般用户自行提取的都是精确匹配内核的，所以未检测到匹配的配置就加载 general（匹配机制为同时在内置，和用户导入配置中匹配，其中优先匹配用户导入的最新配置）」。

### 11.1 链的伪码（可直接实现）

```text
fun resolveProfile(deviceRelease): Result
  label   = kmiLabelOf(deviceRelease)            # release -> major.minor -> label（与 lkm-kmi-manifest.tsv 同源）
  # ①′ 显式指定（forcedBuiltinRelease / PrefBuiltinRelease 一类）——最高优先级，但必须在 UI 可见
  if (explicitRelease != null) return loadBuiltin(explicitRelease)   # 找不到 => 报红（不得静默换）
  # ① 用户导入集合：按 release 精确匹配，取「最新导入」的一份
  if (imported.has(deviceRelease)) return imported.latest(deviceRelease)
  # ② 内置集合：按 release 精确匹配
  if (builtin.has(deviceRelease)) return builtin.get(deviceRelease)
  # ③ 兜底：general-<label>（8 份之一）
  if (generals.has(label)) return generals.get(label)
  # 都不行 => 不可运行（红），并给出「已尝试的三步」
  return Failure(reason = "no-profile: imported/builtin/general all miss", label = label)

# 不变量：以上任何一步都不得「悄悄」换成别的 release；最终选中的来源必须可由 UI 读出
```

### 11.2 判据（逐条可测）

- **精确匹配的集合顺序**：**用户导入 → 内置**（① 优先于 ②）；用户另一句话「一般用户自行提取的都是精确匹配内核的」⇒ **① 是主路径**、**③ 是兜底**；
- **同 release 多份用户导入** ⇒ 取**最新导入**的一份（判定依据见 §11.3）；
- **`general-<label>` 的 label** 由设备 release 派生（`major.minor` ⇒ label 表来自 `lkm-kmi-manifest.tsv`，**不手抄**）；该 label 无 general ⇒ **不可运行**（红）；
- **来源可见性**：解析结果必须带 `source ∈ { explicit, imported, builtin, general }` ⇒ UI 顶部/配置页**显示来源**（与 §2.6 的显式化一致）；运行日志写 `profile_resolved source=… release=…`（现 `AndroidGhostlockRepository.kt:545` 已有同类行）；
- **与既有裁决一致**：走 general ⇒ **不弹二次确认**、**仅当落到非 43284 后端**才轻量标注（U7，§2.6 分支规则）✓。

### 11.3 「最新导入」的判定依据（**选项 + 建议**）

- **选项 A（建议）：存显式导入序号/时间戳**——导入时写一个**单调递增序号**（或 ISO 时间戳）到导入记录里，取序号最大者；**代价**：导入路径要写元数据（一处改动）；**优点**：**确定、可测**、与文件系统无关；
- **选项 B：文件 mtime**——**代价/风险**：脆弱（拷贝、解压、备份恢复、云同步都会改 mtime），且**不同设备/不同文件系统语义不一致**；**不建议**；
- **选项 C：目录列举顺序/文件名排序**——**代价**：隐式（依赖实现），**不可作为语义**；**不建议**；
- **建议**：**A**；并写明「同 release 多份用户配置 ⇒ 取序号最大的那一份」为**唯一规则**（不得按文件名/大小/内容长度等猜）。

### 11.4 并列歧义（各给选项 + 建议）

| # | 情形 | 选项 | 建议 |
|---|---|---|---|
| a | 用户导入里有一份 **general**，内置也有 general | 适用 ① 优先 / 只按内置 | **适用 ① 优先**（规则一致，导出与内置同待遇） |
| b | 用户导入的精确匹配与内置精确匹配 **release 相同** | ① 优先 / ② 优先 | **① 优先**（用户明确要求「优先匹配用户导入的最新配置」） |
| c | 用户导入的 profile **release 与设备不同，但用户手动选了它** | 允许（走新判据）/ 禁止 | **允许**：属**显式选择**，可运行，但按 **§2.9 新判据**检查**所选路径的必须参数**；UI 必须显示来源 = 显式选择 |
| d | `forcedBuiltinRelease` 类偏好（`AndroidProfileConfigController.kt:585`）放在链的哪一步 | ①′ 最高 / 并入 ② | **①′ 最高但必须在 UI 可见**（不允许静默）；**建议在 UI 上把它与「设备 release 精确匹配」并列展示，让用户能一键取消** |

### 11.5 证伪（写进批次门槛）

1. 放一份**用户导入的精确匹配**（新的）⇒ **必须用它**（而不是内置）；
2. 删掉它 ⇒ **必须回落到内置**；
3. 两者都没有 ⇒ **必须走 general**（UI 显示来源 = general）；
4. **静默检查**：任何一步都不得**悄悄**换成别的 release ⇒ UI 必须显示来源；人为构造「显式指定了一个不存在的 release」⇒ **必须报红**而不是回落。

### 11.6 待确认（U19–U22；一律「选项 + 代价 + 建议」）

- **U19**：「最新导入」判定取**显式序号/时间戳**（建议）/ mtime / 目录顺序？
- **U20**：`forcedBuiltinRelease` 类偏好放 **①′ 显式指定（建议，UI 可见）** / 并入 ② 内置匹配？
- **U21**：用户导入的 **general** 是否享 ① 优先？（建议：**是**）
- **U22**：用户手动选择「release 与设备不同」的 profile 是否允许？（建议：**允许**，按新判据校验并显示来源）










