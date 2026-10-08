# 归档头（docs/plan 批次，2026-10-07）

- 原始路径：docs/plan/available-handoff-plan.md
- 归档原因：被 docs/plan/MASTER-PLAN.md 取代；未按现行设计规范编写
- 归档日期：2026-10-07 22:37（America/Toronto）
- 归档来源：task-63（docs-uml）

---


# available 声明纳入 handoff/terminal 计划（2026-10-06）

> 状态：**设计稿，待用户批准**。L 级（跨 Native↔Kotlin 契约 + profile 格式）⇒ 批准前不改代码。
> 提出人：用户（「native 侧 handoff 没有组装在 step 里吗？配置文件和 UI 和执行应当统一」）。
> 撰写：native-r1。范围：`profile-core/src/**` + `app/src/main/kotlin/**`（实施阶段）+ 资产由 kotlin-i18n 落。

## 现状与基线

**执行侧（native）四轴已齐备** —— handoff **不是** step，而是独立轴：

- 分派目录是 (backend, steps, terminal) 三轴：`src/core/contract/identity.hpp:10`；`ComponentSelection` 持有 `TerminalKind terminal`：`identity.hpp:64-68`；
- 词表：`TerminalKind { RootChild = 1, UmhForward = 2 }`：`identity.hpp:36-39`；"user-visible handoff path" 由 `CombinationKind`（`McastRootchild`/`PselectRootchild`/`TcpRootchild`/`McastUmh`/…）：`identity.hpp:115-131` 表达；
- 可用性由 `terminal_available()` 拥有：`identity.hpp:85-87`（`RootChild` 条件、`UmhForward` 恒真），`:364` 在组合选择时调用；
- wire 上 handoff **已经存在**：GLKv3 根标量 `terminal`（文本 token）——解析 `src/core/profile/glkv3.cpp:419-424`（`staged.terminal` / `has_terminal`）、写回 `glkv3.cpp:631-633`；
- 解析器**已有一致性检查**：把根 `terminal` 转成 `TerminalKind` 并要求它等于所匹配组合的 terminal，不等即拒绝：`src/core/profile/glkv3_parse.cpp:358-364`；
- 组合 token 形如 backend · route · stepset · terminal（`contract::kCombinationCatalog`）。

**配置侧（profile 声明）缺 handoff** —— 这就是「不统一」之处：

- `available.<backend>` 的对象形态当前只允许 `route` / `queue` / `experimental` / `priority`：
  Kotlin 权威键集 `profile-core/src/main/kotlin/com/ghostlock/app/data/ProfileLayout.kt:68-69`；
- 声明经 `carryAvailableSelection`（`ProfileLayout.kt:309-318`）投影进 canonical owner，再由 `buildRuntime` 写运行期载体（`ProfileLayout.kt:894-899`），wire 由 `NativeProfileDocument.from()` + `NativeProfileGlkv3Adapter` 发射；
- **App 的根 `terminal` 今天不是声明来的**：`NativeProfileGlkv3Adapter.kt:289` 取 `document.combination?.terminal?.token ?: DEFAULT_TERMINAL`（`DEFAULT_TERMINAL` 见 `:40-44`）⇒ profile 无从表达 handoff，第三级只能由组合 token 或默认值决定。

**UI 侧**：正向「读声明」迁移 ⇒ handoff 无声明即无从展示（kotlin-app 已按指示只做只读、不假装可选）。

## 目标与约束

**目标**

1. 让 `available` 的一个声明项表达一条**完整可执行路径**：backend + route + queue + **handoff**；
2. 配置、UI、执行三处**同一份权威**（声明），不再由组合 token 默认值隐式决定 handoff；
3. 复用既有词汇（`TerminalKind` 的 token），不新造词表；
4. 保持 M5 的跨路径不变量：声明路径与运行期载体路径**逐值等价**。

**非目标 / 硬边界**

- **不碰**被暂停的 `payload` owner 与插件 owner 面（与 terminal/handoff 统一无关）；若实施中发现必须动 payload 段 ⇒ 停下上报；
- 不改变 `available` **既有键**（`route`/`queue`/`experimental`/`priority`）的语义与校验行为；本计划只**新增**一个键；
- 不改 wire 形状（根 `terminal` 已存在），不新增字段、不引入版本号；
- 不在本计划内改攻击路径（waiter/race/step 执行）⇒ 不触发真机门禁；仅当后续把 handoff 纳入**组合选择门禁**时才需要设备验证。

## 决策点（选项 + 代价 + 建议）

### D1 键名：`handoff` 还是 `terminal`？

- **A 用 `handoff`（建议）**：与用户措辞（handoff 给 root child）和 `identity.hpp:136` 的"user-visible handoff path"一致；代价：与 wire 根键名 `terminal` 不同名，需要在文档里一句话对齐（wire 键名不动）。
- B 用 `terminal`：与 wire/manifest 同名，解析器零心智负担；代价：对用户是内部术语，且与"路径"语义（root_child 与 shizuku 都进 root child，只差 step set）易混。
- C 两者并存（别名）：**不建议** —— 一份数据两条真值来源，违反本项目"单一权威"原则（M5 教训）。

建议：**A**。取值词表 = `TerminalKind` token（`root_child` / `umh_forward`），与 `contract::terminal_token_name()` 同源，Kotlin 侧由 `TerminalKind.kt` 对拍（见改动清单）。

### D2 缺省行为（无 `handoff` 字段时）

- **A 保持现状派生（建议）**：沿用 `NativeProfileGlkv3Adapter.kt:289` 的 `combination?.terminal ?: DEFAULT_TERMINAL`。代价：声明与执行仍可能"不显式"，但它**向后兼容 66 份资产**，且 native 的 `glkv3_parse.cpp:358-364` 已保证根 terminal 与组合一致 ⇒ 不会出现"声明一套、执行另一套"。
- B 必填：声明面立刻统一；代价：66 份资产 + 8 份 general 必须同批补 `handoff`，任何漏写都拒绝 ⇒ 大批量资产改动 + 破坏性。
- C 从 route/stepset 派生：省一次书写；代价：**不可靠** —— `identity.hpp:136` 明写 handoff「not derivable from the terminal」，同理也可被 route 反向多义（`McastRootchild`/`McastUmh` 同 route 不同 handoff）⇒ 会引入第二份派生逻辑。

建议：**A**，并在文档与 UI 提示里把"未声明 = 沿用组合默认"写明；后续若用户要求显式化，再走 B（那时是一次**机械补全**，风险可控）。

### D3 多条声明项（同一 backend 多 route / 多 handoff）

- 今天 `available.<backend>` 是**以 backend 为键的 map** ⇒ 每个 backend **只能声明一条路径**；
- **A 保持一项一路径（建议）**：`handoff` 与既有 `route` 一起把这条路径写全；代价：同一 backend 的 `mcast + root_child` 与 `mcast + umh` 无法并存声明。
- B 未来改列表形态（`available = [ { backend = …, route = …, queue = …, handoff = … } ]`）：能表达 N 条路径；代价：**破坏性格式变更**（校验、投影、载体键、资产全改），且与"以 backend 为键"的现有 wire/载体（`backend.queue_selection.<id>`）不再同构 ⇒ 需要独立批次。

建议：**A**（本计划），把 B 记为**后续独立计划**；本计划只保证"一条声明项 = 一条完整路径"的语义成立，为 B 留出扩展位（新增键而非改形态）。

### D4 与 `terminal_available()` 的关系

- 声明 = **请求**；`terminal_available()`（`identity.hpp:85-87`）= **门禁**。
- 建议：声明了 `terminal_available() == false` 的 handoff ⇒ **具名拒绝**（解析/选择期，`plan_error reason=` 风格），**不静默改写**成别的 terminal；
- `RootChild` 的可用性今天依赖所选 step 属主（rootchild/shizuku 路径）⇒ 若声明 `root_child` 而所选组合是 UMH 系 ⇒ 组合不匹配，同样具名拒绝（与 `glkv3_parse.cpp:358-364` 的既有检查同族）。

## 改动清单（实施阶段的 file:line；本计划不改）

### 1. Kotlin：声明解析 / 校验 / 投影 / 发射

| # | 文件:行 | 改动 | 理由 |
| - | ------- | ---- | ---- |
| 1 | `profile-core/.../ProfileLayout.kt:68-69` | `AvailableSelectionKeys` 增 `"handoff"` | 唯一权威键集 |
| 2 | `profile-core/.../ProfileLayout.kt:453-486` | `validateAvailableSelection` 增 `handoff` 分支：必须是字符串且 ∈ `TerminalKind` token 集 | fail-closed，坏值带路径 |
| 3 | `profile-core/.../ProfileLayout.kt:309-318` | `carryAvailableSelection` 把 `handoff` 投影进 canonical owner（键名与 wire 根键一致：`terminal`） | 与 route/queue 同源同路 |
| 4 | `profile-core/.../ProfileLayout.kt:96` | `CarriedSelectionKeys` 增 handoff 的载体键（运行期载体 `<id>.terminal`） | App 交运行期形态时仍可读 |
| 5 | `profile-core/.../ProfileLayout.kt:894-899` | `buildRuntime` 写载体时带上 handoff | 载体与声明逐值等价（M5） |
| 6 | `profile-core/.../NativeProfile.kt:461-478` | `from()` 读 handoff：载体优先、回退 `available.<id>.handoff` | 与 route/queue 同一条两路径规则 |
| 7 | `profile-core/.../profile/NativeProfileGlkv3Adapter.kt:289` | `terminal = document.handoff ?: document.combination?.terminal?.token ?: DEFAULT_TERMINAL` | 声明优先，保持向后兼容缺省 |
| 8 | `profile-core/.../profile/ProfileResolver.kt` | 只读访问器（`declaredHandoff`），供 UI/校验共用 | 避免第二份判据 |
| 9 | `app/src/main/kotlin/.../AndroidProfileConfigController.kt:101-118` | 把 handoff 校验并入既有 invalid 路径集合（与 43284/43499 同一口径） | UI 高亮一致 |

### 2. Native：解析一致性（不新增字段）

| # | 文件:行 | 改动 | 理由 |
| - | ------- | ---- | ---- |
| 10 | `src/core/profile/glkv3_parse.cpp:358-364` | 保留既有"根 terminal 必须等于组合 terminal"；额外：当文档同时声明 queue 选择与根 terminal 时，冲突要给出**具名 reason**（今天只有 `return -1`） | 诊断可读；行为仍是 fail-closed |
| 11 | `src/core/profile/glkv3.cpp:419-424` | 无改动（根 `terminal` 已是权威通道） | 已满足 |

### 3. manifest

- **预计无改动**：`profile-manifest-v3.tsv` 覆盖的是 **owner 段字段 + 根级 owner 绑定标量**（`root\t<key>`），而 `terminal` 是**选择通道**的根 token（与 `route`/`backend` 同类），不在 owner 绑定之列 ⇒ 不新增行；
- 若实施中发现需要"owner 侧回显"（例如 `backend.<id>.terminal`）⇒ 才需要两份 manifest 同步 + `profile_manifest_v3_test.cpp` 对拍（本计划不采取该形态）。

### 4. 资产（由 kotlin-i18n 落）

- 66 份 `app/src/main/assets/profile/*.conf` + 8 份 general：在 `available.<backend>{ … }` 内按各自路径补 `handoff = "root_child"`（43499 + rootchild/shizuku 系）或 `handoff = "umh_forward"`（43284/UMH 系）；
- 这是**可选补全**（D2 建议 A 下缺省仍合法）⇒ 可与本计划分开批次；
- `index.conf` 不动。

### 5. 测试与守卫

- `profile-core`：新增 `AvailableHandoffTest`（键校验、坏值具名拒绝、词表对拍 TerminalKind）；
- **跨路径等价**：声明路径（canonical）与载体路径（runtime）解析出的 handoff 逐值相等；
- **wire 守卫**：声明了 handoff ⇒ 解出的文档根 `terminal` 必须等于它（值等于声明），未声明 ⇒ 仍等于组合默认；
- **证伪**：临时让发射忽略声明 ⇒ 守卫必红 ⇒ 撤回；
- **App**：`Sog10*` 系与 `BuiltinProfilesTest` 的期望按新语义更新（kotlin-i18n 范围）。

## 数据流 / 控制流差异

**今天**

    HOCON available.<id>{route,queue,experimental}   （无 handoff）
      -> carryAvailableSelection -> canonical owner
      -> buildRuntime -> 载体 backend.queue_selection.<id>
      -> NativeProfileDocument.from() -> Adapter.terminal = combination?.terminal ?: DEFAULT
      -> wire 根 terminal  -> glkv3_parse 校验 == 组合 terminal

**改后（D1=A、D2=A）**

    HOCON available.<id>{route,queue,experimental,handoff}
      -> carryAvailableSelection（含 handoff，落到 owner 的 terminal 键）
      -> buildRuntime -> 载体 <id>.terminal
      -> from()（载体优先、回退声明）-> Adapter.terminal = 声明 ?: combination ?: DEFAULT
      -> wire 根 terminal -> glkv3_parse 校验 == 组合 terminal（不变）

**不变量**：① 载体与声明逐值等价（M5）；② wire 根 terminal 与所匹配组合的 terminal 一致（`glkv3_parse.cpp:358-364`）；③ 未声明 handoff 的文档行为与改前**逐字节相同**。

## 兼容性与回滚

- **向后兼容**：缺省沿用组合派生 ⇒ 66 份未改资产仍导出同样的 `.bin`（可用导出产物逐字节对拍证明）；
- **前向兼容**：多写 `handoff` 的资产在旧版本 App 上会因未知键被拒 ⇒ 资产与实现必须**同批**发布（本计划已把资产列为独立批次，实施时需注意）；
- **回滚**：删掉该键 + 撤 7 处 Kotlin 改动即可；native 与 wire 无改动 ⇒ 回滚面小。

## 验证矩阵

| 层级 | 命令 | 预期 |
| ---- | ---- | ---- |
| profile-core 单测 | `./gradlew :profile-core:test --rerun-tasks` | 退出码 0；XML 三桶；新守卫全绿 |
| App 单测 | `./gradlew :app:testDebugUnitTest` | 退出码 0（期望同步后） |
| 构建 | `./gradlew :app:assembleDebug` | 退出码 0，零 `w: file:` |
| 导出 | `./gradlew :profile-core:exportProfiles --rerun-tasks` | 退出码 0；**未改资产时产物逐字节不变** |
| native（若动 10） | `make -C src native-host-tests` + NDK 构建零警告 + `make -C src lint-tidy` | 0 findings |
| 证伪 | 发射忽略声明 / 校验放行坏 token | 守卫必红，撤回后 sha 回位 |

## 明确保留

- `terminal_available()` 的判定逻辑与其 device-gate 语义**不动**（声明只是请求，不改变可用性）；
- wire 根 `terminal` 的名字与类型**不动**；`available` 既有四键行为**不动**；
- `payload` / 插件 owner 面**不动**（用户指令暂停中）；
- `LegacyProfileConverter` 的 v1 转换**不动**。

## UML 影响（同批更新）

- `docs/development/full-process-uml.md` §1 IPO（选择阶段的数据项新增 handoff）；
- §3.1 C++ Class（`contract/identity.hpp` 的 `ComponentSelection`/`terminal_available` 注记）+（若动 10）`profile/glkv3_parse.cpp` 的一致性检查；
- §3.2 Kotlin Class（`ProfileLayout` 键集/投影、`NativeProfileDocument.from`、`NativeProfileGlkv3Adapter`、`ProfileResolver`）；
- §2.7 Kotlin 配置/运行状态机（声明 → 运行期载体的投影顺序）；
- §4.1 端到端 Sequence（App 一般执行 → 内核 → handoff）中 handoff 的来源标注。

## 进度

- [ ] 用户批准本设计（含 D1/D2/D3/D4 四个决策点）
- [ ] Kotlin 声明解析/校验/投影/发射（改动清单 1-9）+ 守卫 + 证伪
- [ ] native 诊断具名化（改动清单 10）+ host 测试
- [ ] 资产补 `handoff`（独立批次，kotlin-i18n）
- [ ] UML 四张图更新
- [ ] 门禁全绿 + 导出产物对拍
