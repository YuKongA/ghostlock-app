# 步骤队列取代 token：L 级设计稿（2026-10-05）

> **状态（v2.2，2026-10-06，定稿）：U5/U9/U10 已按建议裁决并入（§11「全部已裁决」）；实现自 M1 开始（用户面零变化）。v2.1 摘要：用户复裁已并入 —— ① route **回到队列级**（43499 必填 / 43284 不得出现 / 每步写 ⇒ 拒绝；「要设计好」的六条见 §5-Q2）；② **坚持改 wire**：`queue` = **对象数组**，**HOCON 与 wire 同形**（Lead 的点分索引折中已撤回，沿革见 §0.2）；W2 回到「独立大批次」，**需要真机门禁**（§4.5）。语法 = B；seam 为纯占位 + 复用既有插件阶段标识符；键名 = `queue`；UI 重做；新实验一律走生产路径。Lead 复核中。**
> **用户已裁决的两条前提（不再争论）**：① **队列直接取代 token**（理由：HOCON 可读性极大增强）；② **不做**完全动态 DSL / 运行期自适应规划（planner / 状态机）——队列是**静态声明**。
> 命名：本文按 Lead 指定落在 `docs/analysis/step-queue-design.md`（即文档规范里的 L 级「计划」类文档；正式的 `*-plan.md` 归档名在实现批次启动时可改为 `step-queue-plan.md`，本文不复制结构）。

---

## 0 用户裁决与沿革（2026-10-05 / 2026-10-06）

### 0.1 裁决清单（用户原话要点 → 本稿落点）

| # | 用户裁决 | 本稿落点 |
|---|---|---|
| D1 | **选 B（对象数组）** | §5.0/§5-Q1：**B = 规范形态**；A 纯数组**不作为 HOCON 语法保留**（建议：可读性快捷方式放 UI，见 §5-Q1） |
| D2（**已被 D2′ 取代**） | **route 每步带**，43284 可省略 | v2.0 曾写「每步可写、整条一致」；现按 D2′ 执行 |
| **D2′（2026-10-06，现行）** | 「**那算了整体声明一个 route 得了，但是要设计好**」 | §5-Q2 + §10.1-S14：**route 只在队列级**（每步写 ⇒ 拒绝）；**43499 必填**、**43284 不得出现**；位置＝与 `queue` 平级的 backend 字段；进入 `CanonicalPlan.route`，判定与 dispatch 都以它为准 |
| D3 | 插件调用点**纯占位**，但**阶段标识符先标记** | §5-Q3：seam 携带**既有插件阶段词汇**（`pre_spawn`/`post_spawn`/`pre_terminal`/`post_terminal`）作为**预留标识符**；实现仍冻结；R1 位置约束保留 |
| D4 | **UI 要重新改，现有 UI 测试不用测了** | §4.4-M4：UI 重做；旧 UI 测试不作迁移验收（替换/删除在实现批次登记） |
| D5（**形态已被 D5′ 取代**） | **选 W2** + 「以后新实验尽量从已有路径启动，不要为实验加 CLI 旗标/旁路」 | v2.0 曾用 Lead 的「点分索引键」折中落地；现按 D5′ 执行 |
| **D5′（2026-10-06，现行）** | 「**我还是坚持改 wire**」 | §4.5：**wire 直接承载数组结构**（`queue` = 对象数组，**HOCON 与 wire 同形**）；「点分索引键」折中**撤回并标为已被用户否决**（它违背「**wire 应当对齐 profile**」原则）；队列本身仍是「预留扩展性」机制（实验走生产路径，零新增 CLI） |
| D6 | **键名你来定** | §11-U7 **定为结论**：新键 **`queue`**（不复用 `steps`；语义不同，显式优于隐式） |

### 0.2 沿革（旧决定为何被取代，不删历史）

| 时点 | 旧决定 | 现决定 | 取代理由（物证） |
|---|---|---|---|
| 本稿 v1（Lead 评审前） | 每步 route ⇒ **一律声明期拒绝** | v2.0 曾改为「允许每步写、整条一致」；**v2.1 起：只在队列级**（见下一行），「每步换 route」始终不做 | 证据不变（`pipeline.hpp:53-62`：Route 整轮一次 `prepare→execute→disarm→destroy`）；用户复裁要求「整体声明一个 route，但要设计好」 |
| 本稿 v1 | W2 = wire **array of maps**（复合值）⇒ 独立大批次 | W2 = **HOCON B + App 降点分索引键**（wire 仍是既有扁平「段.键」机制）⇒ 中等批次、**不动类型面** | 中立 `Document::Value` 只有 raw/text 两态（C3），复合值成本不成比例；点分索引键有既有先例（冻结的 payload 设计 `payload.ko.<i>.path`） |
| 本稿 v1 | 键名 `queue` 仅"建议" | **结论**：`queue` | 语义与 `steps` 不同；混在一个键里违反显式原则（Lead 裁决） |
| **v2.0**（用户 D2） | 每步 route 可写、整条一致 | **route 只在队列级**：43499 必填 / 43284 不得出现 / 每步写 ⇒ 拒绝 | 用户 2026-10-06 复裁「整体声明一个 route 得了，但是要设计好」；队列取代 token 后 route **没有别的来源** ⇒ 存在性必须显式（§5-Q2） |
| **v2.0**（Lead 折中） | HOCON B + App 降「点分索引键」（不动 wire 类型面） | **wire 直接承载对象数组**（HOCON 与 wire 同形） | 用户 2026-10-06「我还是坚持改 wire」；折中**违背既定「wire 应当对齐 profile」原则**，Lead 已撤回折中（§4.5） |

---

## 1 动机

当前用户可见的选择面是**一个 token**（`backend.<id>.steps`，如 `mcast_rootchild`/`umh`），它一次性蕴含 route + path + step set + terminal。三个问题：

1. **可读性**：profile 里写 `steps = "mcast_rootchild"` 无法表达「跑哪几步」；想跳过/组合步骤只能选预设，用户看不到步骤本身。
2. **扩展性**：步骤顺序与依赖被编码进 C++ 模板（`Cve43499_W1W3`/`Cve43499_W1W2`，`src/core/backend/cve_2026_43499_backend.hpp:61-62`），新增一个步骤组合＝新增模板 + 目录行 + dispatch case，而不是声明。
3. **判定面**：`kCombinationCatalog`（12 行）既是「用户词汇」又是「dispatch 依据」，两个职责绑在一起；一旦用户想写非预设的步骤序列，就没有合法表达。

队列取代 token 后：**用户声明步骤序列**（静态），native 负责**归一化**成内部计划、与**已过真机的预设集合**比对，决定 `supported`（复用已验证路径）还是 `experimental`（显式标记 + 警告 + 需真机门禁）。token 在迁移期降级为**语法糖**，最终删除。

## 2 现状与基线

- 分支/基线：`very-not-stable-dev`，本稿写于 `f72b145f`（守卫批）之后；HOCON 重构 ①–④ 已落地（`b55708a8`、`23958eb0`、`0c63b56b`）。
- 资产：`app/src/main/assets/kernel_profiles/` 共 **68** 个 `.conf`＝**58 个 release profile** + 10 个共享/片段（4 个 `*-template.conf`、`execution-tuning.conf`/`execution-{tcp-zerocopy,select-stack}.conf`、`credential-6x.conf`、`kernelsnitch-6x.conf`、`index.conf`）。（Lead 口径的「62 份」应为早期计数；以 68/58 为准，See §11-U6。）

### 2.1 当前选择面（token）

| 物证 | 说明 |
|---|---|
| `src/core/contract/identity.hpp:116-214` | `kCombinationCatalog` 12 行：token → {backend, route, path, StepSetKind, terminal, available} |
| `identity.hpp:224-234` | `combination_resolve(backend, token)`：**精确、区分大小写、无归一化** |
| `identity.hpp:294-300` | `combination_stepset_wire()`：token → StepSetKind 数值；**未命中返回 0** |
| `profile/glkv3_parse.cpp:207-287` | `resolve_combination()`：从 `backend.<id>.steps` 解析 token，校验 root route/terminal 一致，写 `document.combination` |
| `backend/cve_2026_43499/schema.hpp:88`、`backend/cve_2026_43284/schema.hpp:154` | bind 的 `steps` 存储 lambda 调用 `combination_stepset_wire()` |

### 2.2 当前执行面（step set / dispatch）

| 物证 | 说明 |
|---|---|
| `backend/cve_2026_43499_backend.hpp:61-62` | `Cve43499_W1W3 = Cve2026_43499Backend<W1W3Steps>` / `Cve43499_W1W2 = ...<W1W2Steps>`：**步骤序列是模板参数** |
| `backend/cve_2026_43499/steps.cpp:460`、`:497` | `W1W3Steps::run`（W1→W2→W3）/ `W1W2Steps::run`（W1→W2，跳过 seccomp 绕过） |
| `pipeline/pipeline.hpp:53-62` | `Pipeline<Backend, Terminal>` 的 `steps = Backend::steps`、`target = path_target_of(...)` + `static_assert(target != None)` |
| `pipeline/component_catalog.hpp:46-65` | `path_target_of()`：**硬编码 3 个 (backend, steps, terminal) 三元组** |
| `pipeline/component_catalog.hpp:69-99` | `dispatch_target_of()` / `combination_supported()` |
| `pipeline/orchestrator.hpp:61-86` | 按 `DispatchTarget` 逐 case 的 `static_assert(P::target == ...)` 与 `P::run`（**唯一的组合 wiring 权威**） |
| `contract/identity.hpp:352-357` | `selection_supported()`＝backend_available ∧ stepset_available ∧ terminal_available |

### 2.3 当前词汇与导出物

| 物证 | 说明 |
|---|---|
| `app/src/test/resources/combination-manifest.tsv` | 12 行，列 `token/backend/route/path/**steps**/terminal/available/doc` |
| `app/src/test/resources/vocabulary-manifest.tsv` | **14 行**：4 kind；其中 `stepset` 3 行（`w1_w2`=1、`w1_w3`=2、`pagecache_write`=3；`Unknown=0` 是 native 哨兵，不导出） |
| `src/core/tests/vocabulary_manifest_test.cpp:90-97` | 上述 3 行的导出点（`stepset_name()`） |
| `contract/identity.hpp:54-62` | `StepSetKind{Unknown=0, W1W2=1, W1W3=2, PageCacheWrite=3}` |
| `profile-core/src/main/kotlin/.../Glkv3Encoder.kt:16-36` | Kotlin 值模型已含 `Glkv3Value.Array` |

### 2.4 已知的静默风险点（来自《按名过滤/段拷贝》盘点，队列相关部分）

> 这些是队列落地时**必须逐个处理**的点；带 ⚠ 的是当前**无守卫**、失败时不会报错的。

| # | 位置 | 现形态 | 队列落地时的失效模式 |
|---|---|---|---|
| S1 ⚠ | `identity.hpp:294-300` `combination_stepset_wire()` | 未命中 ⇒ **返回 0** | 任何绕过 `resolve_combination` 的路径（未来队列直接 bind / 测试夹具）静默得到 `Unknown=0` |
| S2 ⚠ | `identity.hpp:251-268` `combination_from_id()` | 未命中 ⇒ `Unknown` | 队列归一化出的 (backend, route, path) 不在表内 ⇒ 下游 `spec==nullptr` ⇒ 默认值 |
| S3 ⚠ | `component_catalog.hpp:142-152` `combination_terminal()/combination_route()` | `spec==nullptr` ⇒ **静默默认 RootChild / None** | 未知计划被当成「root_child + 无 route」继续走 |
| S4 | `component_catalog.hpp:46-65` `path_target_of()` | 硬编码 3 三元组 | 新计划未登记 ⇒ `None` |
| S5 ⚠ | `orchestrator.hpp:83-84` `case DispatchTarget::None` | `return Rejected;` **无任何诊断** | 队列语法合法但未接线 ⇒ 用户只看到 rejected，无原因 |
| S6 ⚠ | `component_catalog.hpp:164-172` `stepset_name()` | default **`"unknown"`** | 新 step/preset 未登记 ⇒ `combination-manifest.tsv` 的 steps 列静默写 `unknown`（与已修的 `owner_for` 同类） |
| S7 ⚠ | `identity.hpp:271-278` `path_name()` / `:284-291` `route_name()` / `:305-314` `backend_token_name()` | default `"unknown"`/`"none"` | 两张 manifest 静默写兜底值 |
| S8 | `profile/glkv3_parse.cpp:154-166` `known_owner_section()` | 前缀白名单（仅 `backend.`） | 队列放新段 ⇒ 放行；未声明 ⇒ bind UnknownSection（**loud**）；放**根级** ⇒ 走根键 4 处通道，漏 `frame_v3` 静默（已在 `glkv3.hpp:190-206` 注释化） |
| S9 | `profile/glkv3_parse.cpp:207-231` `resolve_combination()` | 要求 `backend.<id>.steps` 存在且可解析 | 队列取代 `steps` 键 ⇒ 不改此处则所有新语法文档被拒（**loud**，但会造成"设计已落地却全拒"） |
| S10 ⚠ | `combination_manifest_test.cpp` / `vocabulary_manifest_test.cpp` | 无「名字兜底/行数/集合」守卫 | 队列新增 step 词表后，导出物可静默弱化（对照：profile manifest 已于 `f72b145f` 加三条守卫） |
| S11 | wire 类型面：`glkv3_parse.cpp:363` 拒 Array；`profile/schema.hpp` FieldSpec 无 array kind；Kotlin 已有 `Glkv3Value.Array` | 队列若以数组进 wire ⇒ native 侧"未实现即拒"（loud），集成成本在 native |

## 3 目标与约束

**目标**
1. 用户可在 HOCON 里**静态声明步骤序列**（可读、可带参数、可标 seam 位置），不再只能选预设 token。
2. **native 是步骤词汇的唯一权威**；未注册步骤 id **编译不过**（不是运行期才拒）。
3. 每个非法声明形状 ⇒ **声明期拒绝整个文档**；每个非法形状都有一个**能被证明失败**的守卫。
4. 归一化后的计划与**已过真机的预设集合**比对 ⇒ `supported`（复用已验证路径）/ `experimental`（显式标记 + 警告 + 需真机门禁）。
5. token 迁移期作语法糖，**资产改写完成后删除**（禁止长期双真相）。

**非目标（明确不做）**
- 不做运行期自适应规划 / planner / 状态机（用户裁决 ②）；队列是静态声明，计划在解析期完全确定。
- 不做动态 DSL（条件、循环、变量、表达式）；队列元素只有「步骤 / 参数 / seam」三类。
- 不改攻击原语本身（waiter/race/route 实现）；本设计只改**选择面 + 计划归一化 + 分派**。
- 不恢复插件工程（仍冻结；seam 只做惰性占位，见 §5-Q3）。

## 4 设计

### 4.1 ① 步骤词汇（native 权威）

**表位置**：新增 `src/core/contract/step_catalog.hpp`（`contract/`，**host 可编译、不 include `backend/`/`pipeline/`**，遵守 R1 防火墙）。

**每步字段（StepSpec）**

| 字段 | 语义 | 现状取值（迁移起点） |
|---|---|---|
| `id` | HOCON 里写的步骤 id | `w1` / `w2` / `w3` / `pagecache_write` |
| `display` | UI/诊断显示名 | `SELinux bypass` / `credential & uid 0` / `seccomp bypass` / `page cache write` |
| `backend` | 所属 backend（**不可跨 backend 混装**） | `cve_2026_43499` ×3；`cve_2026_43284` ×1 |
| `slot` | 在该 backend 规范序中的位置（**允许位置**用区间表达） | w1=0、w2=1、w3=2、pagecache_write=0 |
| `deps` | 前置依赖（位掩码/id 集合） | w1=∅、w2={w1}、w3={w1,w2}、pagecache_write=∅ |
| `skippable` | 是否可单独省略 | w1/w2/w3 均**不可**单独跳（跳 w3 只能用预设 `w1_w2` 的等价队列 `[w1,w2]`）；pagecache_write 不可 |
| `available` | 是否有**已过真机**的执行路径 | w1/w2/w3 与 pagecache_write 均 true（三者组合的可用性见 §4.3） |
| `effects` | 副作用域（供权限/日志/审计与「实验面」提示） | w1=SELinux state；w2=credentials(uid/caps)；w3=seccomp；pagecache_write=page cache |

**编译期注册（未注册 id 编译不过）**
- `inline constexpr StepSpec kStepCatalog[]` + `static_assert`：id 唯一、token 唯一、同 backend 内 slot 唯一且连续、`deps` 只指向同 backend 且 slot 更小的步骤。
- 每个执行体（backend step policy）声明 `static constexpr StepId step_id`；用 concept `StepExecution<Exec>` + 折叠 `static_assert((has_executor<Execs>(spec.id) && ...))` 把**目录表与执行体列表绑死**：
  - 目录里有 id → 必须有执行体（否则编译错）；
  - 执行体有 id → 必须在目录里且唯一（否则编译错）。
- 迁移起点：`W1W3Steps` ≡ `[w1,w2,w3]`、`W1W2Steps` ≡ `[w1,w2]`（`steps.cpp:460/497`）、43284 `pagecache_write` ≡ 今天 `PageCacheWrite` 的步骤集。**这只是别名映射，不改任何执行体代码**。

**与现有词汇的关系**：`StepSetKind{1,2,3}`（`identity.hpp:54-62`）与 `vocabulary-manifest.tsv` 的 3 行 `stepset` 保留为**内部归一化 id**（wire 数值不变）；步骤 id 词表是**新的、更细的一层**（一次 step set 展开成有序步骤）。两层映射必须在同一张表里声明并可对拍，禁止两处各写一份。

### 4.2 ② 执行侧：注册 + 自检 + 声明期 fail-closed

原则 4 的「逐组合 static_assert」左移为三层：

**(a) 编译期（词汇与执行体）**
- §4.1 的注册断言：未注册 id 编译不过。
- 每个**已验证预设计划**保持 `Pipeline` 的 `static_assert(target != None)`（`pipeline.hpp:59-60`）与 orchestrator 逐 case 的 `static_assert(P::target == ...)`（`orchestrator.hpp:65-66/72-73/79-80`）。**队列不取消这些断言**：队列归一化后仍必须落到一个编译期已实例化的方案上。

**(b) 启动期自检（native，PI 窗口之外）**
对归一化后的计划逐项检查，任一失败 ⇒ `Rejected`（拒绝整个文档，绝不部分执行）：
1. 每步都能解析到执行体（编译期已保证，运行期再断言一次：防御 ABI/注册表被改）；
2. 依赖满足：`deps ⊆ 计划`；
3. 顺序合法：`slot` 单调递增且等于规范序（迁移期严格等于；见 §11-U4）；
4. 计划能解析到 `DispatchTarget`（`component_catalog.hpp:69-76`）；
5. 计划与 backend 的 route 轴一致（43284 无 route 轴 ⇒ 队列不得带 route）。

**(c) 声明期 fail-closed（HOCON 解析/归一化，Kotlin+extractor 侧先做，native 侧复核）**

| 非法形状 | 判定 | 结果 |
|---|---|---|
| 未知步骤 id | 不在 `kStepCatalog` | **拒绝整个文档** |
| 跨 backend 混装 | 步骤 `backend` ≠ 队列所属 backend | 拒绝 |
| 顺序非法 | `slot` 非单调 / 与规范序不符 | 拒绝 |
| 缺依赖 | `deps ⊄ 计划`（如 `[w1,w3]`） | 拒绝 |
| 重复步骤 | 同一 id 出现两次 | 拒绝 |
| 空队列 | `[]` | 拒绝（"什么都不做"不是合法攻击声明） |
| **每步 route**（§5-Q2） | `route` 写进数组元素 | **声明期拒绝**：`step-route-not-allowed`（不是忽略、不是取最后一个） |
| **route 缺失**（§5-Q2） | 43499（有 route 轴）队列没有队列级 `route` | **声明期拒绝**：`route-required`（队列取代 token 后 route **没有别的来源**） |
| **route 不适用**（§5-Q2） | 无 route 轴的 backend（43284）出现 `route` | **声明期拒绝**：`route-not-applicable` |
| **route 重复**（§5-Q2） | 队列级 `route` 出现多处 | **声明期拒绝**：`route-duplicated` |
| **seam 阶段/位置不符**（§5-Q3） | seam 声明的阶段与它所在位置推导的阶段不符，或落在 R1 禁区 | **声明期拒绝**：`seam-stage-illegal` |

**(d) 守卫要求（AGENTS 强制）**：上表**每一行**在实现批次里都要有一个守卫，并按「守卫必须能失败」的规范做证伪实验（临时造错 → 观察到预期失败 → 撤回 → 核验无残留；**证伪一律 `make -B`**，因同秒 mtime 会跑旧二进制给出假证明）。同时，§2.4 的**七个静默默认点必须逐个改成硬失败并配具名诊断**：**S1、S3、S5、S6、S8、S9、S13**（S5 是队列下最危险的一条：`DispatchTarget::None` 会让 orchestrator **不打印任何东西就 `Rejected`**，用户只看到"什么都没发生"）。**每条的诊断文本、落点与证伪方式见 §10.1**——诊断文本要进测试断言，否则会漂移。
形状表里的 route 四条（**每步 route / 缺失 / 不适用 / 重复**）与 **seam 阶段**一条同样要有具名诊断与证伪实验（§10.1 的 S14a–S14d 与 S15 已列出草案文本）。

### 4.3 ③ 支持面与实验面

**归一化**：`queue → CanonicalPlan { backend, route, [step id...], path/terminal }`（步骤去参数化后的规范形式；参数不影响 supported/experimental 判定，只影响执行细节）。

**判定**：
- **与已验证预设相等**（backend/route/path/步骤序列全等）⇒ `supported`：复用该预设**已过真机**的 `DispatchTarget` 与 `Pipeline` 实例，执行路径与今天逐字节一致（迁移期必须保持这一点，见 §7 不变量）。
- **合法但不等**（例如新的步骤子集/顺序）⇒ `experimental`：
  - native 输出**具名诊断**（例：`plan experimental backend=cve_2026_43499 steps=w1,w2 route=multicast_waiter reason=not-a-verified-preset`）；
  - App 在运行前**显式标记**（"实验组合，需真机验证"）；
  - **需要 HOCON 静态声明 `experimental`**（U5 已裁决；静态声明，非自适应），缺省 **不执行**；
  - 真机门禁通过后，才在 `kVerifiedPresets` 里升级为 `supported`（单行数据变更 + 门禁归档）。
- **不等于任何预设且未声明 `experimental`** ⇒ 拒绝（fail-closed）：`plan_error reason=experimental-not-declared backend=<b> plan=<...>`。

**放行一个实验组合的操作步骤（2026-10-06 裁决：本批不放行任何组合；将来解锁照此执行）**

1. **写目录行**：在 `contract/identity.hpp` 的 `kCombinationCatalog` 里为该组合建一行（`backend/route/path/steps/terminal/available=false`），必要时补 `CombinationKind` 枚举值；
2. **接线分派**：`pipeline/component_catalog.hpp` 加 `DispatchTarget` 值并在 `path_target_of()`/`dispatch_target_of()` 返回它；
3. **编译期锁定**：`pipeline/orchestrator.hpp` 加一个 `case` + `static_assert(P::target == DispatchTarget::X)`；**漏一步就编译不过**（这是 §4.2(a) 的既有机制）；
4. **准备执行体**：若步骤序列是新的，backend 侧提供对应 step set（`StepSetKind` + `kStepCatalog`/`kStepSetAliases` 覆盖 + 编译期守卫），并确保 `Pipeline<Backend, Terminal>` 有该组合的实例化；
5. **让它变成"已验证预设"**：**唯一条件** = 归一化后的 `(backend, route, steps)` 与 `kCombinationCatalog` 中 **`available=true`** 的行逐字段相等（`step_plan.hpp::match_verified_preset`）。因此把该行 `available` 置 `true`（单行数据变更）即可让 `verdict` 从 `Experimental` 变 `Supported`；**在此之前**该计划一律以 `plan_error reason=experimental-not-verified` 拒绝（M2 行为）；
6. **真机门禁**：冷机、固定 CPU 对、KernelSU 未加载，跑该组合的正向路径 + 写验证，归档到 `docs/analysis/device-gates/`；
7. **回归清单**：三绿（host/lint/NDK）+ 防火墙 + `queue_wire_test` 的同形对拍语料（新预设自动进入语料，无需手改）+ manifest 重生成（若新增字段）。

**U5 配套规则（用户裁决 2026-10-06，三条）**：
1. **未声明 + 未验证 ⇒ 拒绝**（诊断见上）；
2. **声明了但归一化后等于已验证预设 ⇒ 按 `supported` 跑**——**声明只是"请求"，结论由归一化计算**；既不允许"自称 supported"，也不允许把已验证预设降级成 experimental；
3. **日志记录判定结果**：每个计划一行 `plan verdict=supported|experimental declared=<0|1> backend=<b> steps=[...] route=<r>`（可 grep，进运行日志）。

**`kCombinationCatalog` 的降级**：12 行 token 表**保留**为「内部归一化键」——`combination_resolve/combination_spec/dispatch_target_of` 的输入不再是用户写的字符串，而是归一化后算出的 `CombinationKind`。`available` 列语义改为「已验证预设」（= `supported`），App 下拉框改为「队列编辑器 + 预设列表」。

### 4.4 ④ 迁移策略

**前提（用户澄清 2026-10-06，影响 M2–M5）：v3 尚未正式发布 ⇒ v3 形状变更无需兼容；需要兼容的只有 v1/v2。**
- 因此 **wire/GLKv3 的形状可以自由改**（本设计选 W2：`queue` 直接承载对象数组），**不需要**为「外部 v3 文档」保留旧解析；
- **v1/v2 的兼容在 Kotlin 侧**（唯一的迁移点 `LegacyProfileConverter.kt`；native 仍 **v3-only**，见 AGENTS「版本号统一为 3」）；
- ⇒ **M2 的 token 语法糖只为仓库内 68 个资产服务**（不是对外兼容承诺），**M5 删糖无兼容代价**；M5 的负例「出现 token ⇒ 拒」同样**无兼容代价**。

| 批 | 内容 | 验收 |
|---|---|---|
| **M0** | 本设计稿评审 + 三个语法问题裁决（§5） | 用户签字 |
| **M1 / M1.1** | contract：`step_catalog.hpp`（目录 + 别名 + 编译期注册，**M1 已提交 `bcb94253`**）+ 静默点守卫 + **`step_plan.hpp` 归一化纯函数（M1.1：`CanonicalPlan`、全部声明期判定、supported/experimental 结论）**；**用户面零变化**（assets 不动，仍发 token；M1.1 不接生产） | host/lint/NDK 三绿 + 每条新守卫的证伪实验 |
| **M2** | wire/native：**W2（wire 直接承载对象数组）**+ 中立 `Document` 复合值 + 声明期/启动期 fail-closed + route 三条守卫（S14a–S14d）+ token **语法糖**（解析期展开为队列）+ §10.1 静默点改硬失败与具名诊断 | 三绿 + manifest 重生成 + 防火墙 + **逐条负例证伪** + **同形对拍语料**（wire → native 解析 ⇒ 与 HOCON 语义逐字段一致）+ **真机门禁**（M2 验收判据见 §4.5：supported 逐字节同路径 / 每个 experimental 各自门禁 / 无队列零新增字节） |
| **M3** | assets：58 个 release profile + 4 个 template + `index.conf` 的 `available{}` 改写为队列（其余 10 个共享片段中只有承载选择的那几个要改） | 逐资产「归一化计划 == 旧 token 计划」对拍（沿用 R3「扁平化等价 67/67」的做法与证据格式），0 例外 |
| **M4** | Kotlin/UI：队列解析（B 形态）+ **UI 重做**（用户裁决 D4：现有一版不用了，旧 UI 测试不作迁移验收，替换/删除在实现批次登记）；extractor `--format conf` 同步产出 B 形态 | `:profile-core:test :app:testDebugUnitTest` 绿 + 跨语言 golden 重生成（`make -C src glkv3-golden-hex`）+ Rust `cargo test --release` |
| **M5** | **删除 token 糖**：`backend.<id>.steps` 不再接受 token 字符串（或整键移除，由 §11-U3 决定）+ 负例（出现 token ⇒ 拒）+ 更新两张 manifest 与文档 | 三绿 + 负例证伪 + 真机门禁（43284 全链 + 43499 冷启）+ 门禁归档 |

**禁止长期双真相**：M5 是计划的一部分，不是"以后再说"；M2–M4 期间每次改动都必须保证「归一化等价」，任何一处不等价即视为迁移缺陷（比对脚本进测试）。

### 4.5 wire 承载：**W2 = wire 直接承载数组**（HOCON 与 wire 同形）

> 用户裁决（D5 + D5′）：**选 W2**，且 2026-10-06 明确「**我还是坚持改 wire**」。Lead 的「点分索引键」折中**已撤回**——它违背既定的「**wire 应当对齐 profile**」原则；该方案**整节删除**，只在 §0.2 沿革里保留（被否决的记录）。

**形态（HOCON 与 wire 同形；不再有「两形态归一化」）**

```hocon
# HOCON（用户写什么，wire 就是什么）
backend.cve_2026_43499 {
  route = "select_stack"          # 队列级；43499 必填（§5-Q2）
  queue = [ { step = "w1" }, { step = "w2" }, { step = "w3" } ]
}
backend.cve_2026_43284 {
  # 无 route 轴 ⇒ 不得写 route
  queue = [ { step = "pagecache_write" }, { seam = "plugin", stage = "post_terminal" } ]
}
```

```text
# wire（App 原样发射；native 解析同一形状）
backend.cve_2026_43499 { route: "select_stack", queue: [ {step:"w1"}, {step:"w2"}, {step:"w3"} ] }
```
（wire 是 MessagePack：`queue` = **array of map**；元素恰好含 `step` 或 `seam` 之一。）

**收益（用户选择的正当理由，如实写）**
1. **单一形态** ⇒ 少掉一整类「两种写法等价性」的静默风险（不再需要跨形态一致性语料）；
2. **符合既定原则**：**wire 与 profile 对齐** —— 阅读 profile / 运行日志 / 文档时不需要再做一次「降级映射」的心算，排障时 HOCON 与文档逐字对应；
3. 实验面（非预设组合）天然可表达，且 **native 是归一化权威**（与 §3 目标 2 一致）。

**canonical 分离（`queue_route`）与唯一映射点（2026-10-06 落地 = `4c20142f`）**

- **为什么必须先解决 route 撞键**：几何 `route`（`backend.<id>.route` 是 **Map**，route 私有参数）与**队列级 route**（string）**同名**；若直接写入 canonical 的 `route`，几何 Map 会被覆盖 ⇒ **68 资产的几何静默归零**（正是 §2.4 那类静默风险）。
- **canonical 只多一个键**：`ProfileLayout.validateAvailable` 接受**双形态** `available`（旧的**列表**形态**零破坏**；新对象形态 `<backend>{ route=<str>, queue=[{…}], experimental=<bool> }`），canonical 在**唯一一处**把三键归一进 `backend.<id>`，其中队列级 route 落在 **`queue_route`（str，canonical-only）**，**几何 Map `route` 一律不覆盖**。
- **唯一映射点 = `NativeProfile.backendSection()`**：`queue_route` → **wire 键 `route`**（wire 形态不变，仍是 §4.5 的 `route: "select_stack"`）；**无几何时直接用 `route`**；**`route` 与 `queue_route` 同时为 str ⇒ fail-closed「refusing to pick a precedence」**（不猜优先级）。
- **回显只认「与 `available` 声明逐值相等」**：手工只在 `backend.<id>` 写 route/queue（未在 `available` 声明）⇒ **未知键拒绝**；回显不是「读回任意键」，而是「与声明比对」。
- **完成判据（已达成）**：Kotlin golden（**3964 字符**）与 `make -C src glkv3-golden-hex` **逐字符相同**；两侧 fixture 逐字段对拍（native `--dump-fixture` **91 行** ⇒ `profile-core/src/test/resources/glkv3-native-fixture.tsv`）；**未声明三键 ⇒ 零新增字节**（与 pre-M2 hex 相同）；**68 资产几何零变化**（`ProfileLayoutEquivalenceTest` ≥60 资产 flatten 对拍绿）；**5 条证伪**（删一侧发射 / 未声明也写 / 回显放宽 ×2 / 双 str route 守卫）；门禁 `:profile-core:test :app:testDebugUnitTest --rerun-tasks` **EXIT=0**。

**成本重估（C1–C7 **上调**；按工作量从大到小）**

| # | 工作项 | 说明（wire 直接承载数组） | 量级 |
|---|---|---|---|
| C1 | **放行 `Array`** | `profile/glkv3_parse.cpp:363` 现在对段内 Array 值直接 `return -1`；需开通道：数组长度上限、元素必须是 map、嵌套深度、fail-closed | 中-大 |
| C2 | **复合值承载（本轮最大项）** | 中立 `profile/document.hpp` 的 `Value` 只有 raw / text 两态；`queue` 的元素是 **map**（多层）⇒ 需要**有界复合值**承载 + 字符串视图仍指向 decode buffer 的生命周期约定 + 深度/大小上限 | **大** |
| C3 | **FieldSpec / bind** | 需要 array-of-map 的声明与绑定（或专用 queue 值类型）；bind 后聚合成有序 `CanonicalPlan` | 中 |
| C4 | **manifest / 类型面** | manifest 需表达「array of map + 元素形状」；Kotlin `Glkv3Value.Array` **已有**（`Glkv3Encoder.kt:35`），但 **manifest 驱动的 adapter 要学新 kind**（元素形状校验：恰好一个 `step` 或 `seam`） | 中 |
| C5 | **元素形状校验 + 守卫** | 恰好一个 `step` 或 `seam`；未知键 ⇒ 拒绝；空数组 / 非 map 元素 / 重复 step / 依赖与顺序（§4.2 形状表）+ 每条守卫证伪 | 中 |
| C6 | **同形对拍语料**（替代原「跨形态一致性语料」） | App 发射 → native 解析得到的 `CanonicalPlan` 必须与 **HOCON 语义逐字段一致**；语料覆盖合法/非法元素形状 | 中 |
| C7 | **跨端产出/编辑** | extractor `--format conf` 产出对象数组；Kotlin 解析/发射（编码器已就绪）+ **UI 重做**（D4） | 中 |

- **规模判断（如实）**：wire 契约（**类型面**）变更 + 中立 `Document` 复合值 + 跨三端 ⇒ **回到「独立大批次」**，与 M1 分开、不与其它批次合并；**C2 是全批最大风险项**（改的是所有 owner 共用的中立承载）。
- **门禁口径（wire 契约变更）**：
  - **需要真机门禁**；**M2 验收判据**：① `supported` 计划与今天**逐字节同路径**（43284 全链 + 43499 冷启）；② 每条 `experimental` 计划**各自**门禁通过后才升级为 `supported`；③ 无队列文档（含旧 token 文档）**零新增字节**；
  - 需要 manifest 重生成（两份逐字节）+ 三绿 + 防火墙（`180/4/4/0/0`）；
  - 与「新实验走生产路径」一致：**零新增 CLI 旗标**，实验通过同一文档 + 同一 Pipeline 进行。

### 4.6 加一个步骤要付多少代价（正面回答用户疑问：编译期检查会不会更繁琐）

**结论：会略繁琐一点，但只是机械的三处改动，换回的是「未注册 ⇒ 编译不过」的保证；不建议为省事退回纯运行期注册。**

| # | 改动 | 体量 | 漏了会怎样 |
|---|---|---|---|
| 1 | 在 `kStepCatalog` 加一行 `StepSpec`（id/display/backend/slot/deps/skippable/available/effects） | 1 行 | `static_assert` 立刻报错（slot/依赖/唯一性） |
| 2 | 写该步骤的执行体，声明 `static constexpr StepId step_id` | 一个新 policy（与今天加一步的体量相同） | 「目录 ↔ 执行体」折叠 `static_assert` 编译不过，错误信息指到缺哪个 id |
| 3 | 若该步骤要进某个已验证预设：在 `kVerifiedPresets` 加一行（或让既有预设引用它） | 1 行 | 计划归一到 `experimental`（有具名诊断，不静默） |
| （自动） | 步骤词表 / manifest / UI 预设从 `kStepCatalog` **生成** | 0 | 导出物守卫会红（§10.1-S13） |

**降繁琐的三条措施**
1. **单一权威生成**：步骤词表、manifest、UI 预设全部从 `kStepCatalog` 生成（沿用既有 manifest 制度），任何一端都不得手抄；
2. **脚手架清单**：`docs/development/adding-a-component.md` 增一节「加一个步骤：3 处改动 + 自检命令」，照抄即可；
3. **错误指路**：`static_assert` 的消息写明「去 `kStepCatalog` 加/改哪一行」，不要只给一句 `constraint not satisfied`。

**反面对比（纯运行期注册）**：省掉第 1/2 处的编译期绑定，但会**丢掉「未注册 id 编译不过」**，并复活 S5 类风险（未注册/未接线 ⇒ `DispatchTarget::None` ⇒ **静默 `Rejected` 且无诊断**）。

**裁决建议（写进设计）**：**native 内建步骤 = 编译期注册**（本节的 3 处 + 自动检查）；**将来第三方/插件步骤 = 运行期注册 + 探针校验**（插件 ABI 已有描述符/探针机制），两套并存；**不得**为了插件方便而把内建步骤降级为运行期注册。

## 5 语法（**用户裁决已下**：B 为规范形态；本节为落定说明 + 遗留项）

### 5.0 规范形态 = B（对象数组）

**B（规范形态，D1）**

```hocon
backend.cve_2026_43499 {
  route = "select_stack"        # 队列级（Q2：43499 必填；每步不得写 route）
  queue = [
    { step = "w1" },
    { step = "w2" },
    { step = "w3" }
  ]
}
```

**wire 形态（与 HOCON **同形**；§4.5）**

```text
backend.cve_2026_43499 { route: "select_stack", queue: [ {step:"w1"}, {step:"w2"}, {step:"w3"} ] }
```
（MessagePack：`queue` = array of map；**HOCON 与 wire 不做降级映射**。）

**A（纯数组）的处理：不作为 HOCON 语法保留（建议）**
- 理由：B 已能表达 A 的全部语义（`{ step = "w1" }` ≡ 字符串 `"w1"`，但裁决取 B ⇒ 统一写法），保留 A 只会带来**第二套解析/校验/语料/诊断**与「写法不同、判定不同」的额外风险面；
- 可读性诉求改在 **UI 层**满足（预设按钮/快捷输入 → UI 展开为 B 后写入 profile），**不进 profile 语法**；
- 若用户坚持 HOCON 里也能写纯字符串数组，则它只能作为 **App 侧语法糖**（App 展开为**对象数组**再发射；**native 只认对象数组**，不引入第二种 wire 表示）⇒ 记为遗留项 **U9**。

### Q1（已裁决 D1 + D5′）队列形态 = B（对象数组），**HOCON 与 wire 同形**
- **元素只有两种语义**：`{ step = "<id>" }` 或 `{ seam = "<type>", stage = "<stage>" }`（seam 见 Q3）；map **不得**同时带 `step` 与 `seam` ⇒ **拒绝**。
- **A 不保留（U9 裁决）**：纯字符串元素（含整个 `queue` 是字符串数组）⇒ **拒绝** + 具名诊断 `plan_error reason=queue-element-not-object at=<i>`；可读性交给 UI 预设按钮。
- **`params` 预留未实现（U10 裁决）**：`params` 是**已知但保留**的键 ⇒ 拒绝，但报**具名 reserved**（不是 unknown）：`plan_error reason=params-reserved-for-future-step-parameters at=<i>`；契约与 manifest 文档行写明「reserved, not implemented」。
- 其余未知键 ⇒ 拒绝（`plan_error reason=queue-element-unknown-key at=<i> key=<k>`）。
- **空 `queue` ⇒ 拒绝**（`4c20142f` 补）：不得用空数组表达「什么都不跑」；
- **`stage` 必须随 `seam`**（`4c20142f` 补）：只写 `stage` 不写 `seam`、或 `seam` 缺 `stage` ⇒ 拒绝；
- **`available` 双形态（`4c20142f`）**：列表形态（旧）**零破坏**；对象形态 `<backend>{ route=<str>, queue=[{…}], experimental=<bool> }` **五类拒绝**——纯字符串元素 / `route` 与 `queue` 都给 / 都没有 / 未知键 / `params` 保留但拒。
- **同形对拍语料（硬要求，进 M1/M2 验收；替代原「跨形态一致性语料」）**：
  1. **正例**：App 发射的 wire 字节由 native 解析得到的 `CanonicalPlan`，必须与 **HOCON 语义逐字段一致**（HOCON 写 `{step="w1"}`，wire 就是 `{step:"w1"}`；**不做降级映射**）；
  2. **负例**：`queue` 不是数组、元素不是 map、map 既无 `step`/`seam`、同时带两者、带未知键、空数组、重复 step、依赖/顺序非法、route 位置非法（§5-Q2）⇒ **拒绝**；
  3. **token 糖等价性**（迁移期仍需要）：任一 token 与它的队列糖必须归一化到**同一个内部键**，否则出现「写法不同、支持面判定不同」的隐性坑；
  4. 语料沿用插件探针语料的「**目录即结论**」做法（`accept/`、`reject/` + manifest + walker 自证完备）。

### Q2（**2026-10-06 复裁，现行**）route **只在队列级**，且「要设计好」
- **位置**：`route` 是 backend 条目的**同级字段**（与 `queue` 平级），**不得**写进数组元素。
- **存在性（43499，有 route 轴）**：**必填** —— 队列取代 token 后 route **没有别的来源**。省略 ⇒ 拒绝：`plan_error reason=route-required backend=cve_2026_43499`。
- **适用性（43284，无 route 轴）**：**不得出现**；出现 ⇒ 拒绝：`plan_error reason=route-not-applicable backend=cve_2026_43284`。
- **每步 route** ⇒ 声明期拒绝：`plan_error reason=step-route-not-allowed at=<i>`（不是忽略、不是取最后一个）。
- **唯一性**：队列级 route 恰好一处；重复声明 ⇒ 拒绝：`plan_error reason=route-duplicated`。
- **与归一化的关系**：route 进入 `CanonicalPlan.route`；`supported`/`experimental` 判定、`dispatch_target_of` 与「声明 route == 预设 route」一致性校验**都以它为准**，**不再由 token 隐含**。
- **明确不做**：真正的「**每步换 route**」（一次运行内多轮 `prepare`/`disarm` + waiter 生命周期重排）属**攻击路径改动** ⇒ 独立 L 级设计 + 真机门禁，**本设计不做**。证据：`pipeline.hpp:53-62`（`Pipeline` 只实例化一条 route；`Route` 满足整轮 `prepare→execute→disarm→destroy`）。
- **canonical 承载（2026-10-06 落地 = `4c20142f`）**：canonical 里队列级 route 走 **`queue_route`（str，canonical-only）**，几何 Map `route` **不被覆盖**；**唯一映射点 = `NativeProfile.backendSection()`**（`queue_route` → wire 键 `route`；无几何时直接用 `route`；**两者同时为 str ⇒ fail-closed「refusing to pick a precedence」**）；**回显只认「与 `available` 声明逐值相等」**（手工只在 `backend.<id>` 写 ⇒ 未知键拒绝）。**沿革（为什么必须先解决）**：`route` 撞键会让**几何 Map 被静默覆盖 ⇒ 68 资产几何归零**；完成判据 = Kotlin golden 3964 字符与 `make -C src glkv3-golden-hex` **逐字符相同** + 两侧 fixture 91 行对拍 + **未声明三键零新增字节** + `ProfileLayoutEquivalenceTest`（≥60 资产）绿 + **5 条证伪** + `:profile-core:test :app:testDebugUnitTest --rerun-tasks` EXIT=0。
- **沿革**：v1「每步 route 一律拒绝」→ v2.0「每步可写但整条一致」（D2）→ **v2.1「只在队列级 + 必填 / 不得出现 / 唯一」**（D2′，2026-10-06）。

### Q3（已裁决 D3）seam = 纯占位 + 复用既有插件阶段标识符
- **语法**：`{ seam = "plugin", stage = "pre_spawn" }`（`stage` 必填；`plugin` 是 seam 类型的保留名）。
- **阶段标识符 = 单一权威**：复用**冻结的插件设计**已定义的阶段词汇 `pre_spawn` / `post_spawn` / `pre_terminal` / `post_terminal`（来源：`plugin.<id>.stage`，见 `docs/analysis/contract-design.md` §3.14.7 与 `plugin/schema.hpp`），**不新造第二套**；登记为**预留标识符**（实现仍冻结：队列侧只校验，不加载、不映射）。
- **位置约束（R1）**：PI waiter 存活期**不得**映射插件 `.so`（`plugin/host.hpp:107` 的 `WaiterClosed` 窗口即物化）。⇒ 队列里 seam 的合法位置只有两类：**步骤队列之前**（= `pre_spawn`）与**终态/驻留窗口之后**（= `post_terminal`）；落在 `[w1..w3]` 区间内（会推导出 `post_spawn`/`pre_terminal`）⇒ **声明期拒绝**，诊断：`plan_error reason=seam-stage-illegal stage=<s> at=<i> allowed=[pre_spawn,post_terminal]`。
- **一致性**：seam 的 `stage` 必须与**位置推导的阶段**一致（显式声明 + 交叉校验，不一致 ⇒ 拒绝）；每个 backend 声明自己合法的阶段集合（43284 走 `/dev/glk` 驻留窗口 = `post_terminal`）。
- **将来解冻**：`{ seam = "plugin", plugin = "<id>" }` 作为**扩展键**加入（描述符校验），不改位置/阶段语法。

## 6 改动清单（未来批次，文件级）

| 批次 | 文件 | 改动 |
|---|---|---|
| M1 | `src/core/contract/step_catalog.hpp`（新） | StepSpec 表 + 注册断言 + 归一化纯函数 + 非法形状枚举 |
| M1 | `src/core/contract/identity.hpp` | `StepSetKind` ↔ 步骤序映射（单一权威）；**不改 12 行 token 表** |
| M1 | `src/core/tests/step_catalog_test.cpp`（新） | 词汇/依赖/顺序/归一化 + 每条负例（含证伪实验记录） |
| M2 | `src/core/profile/glkv3.cpp`、`glkv3.hpp`、`glkv3_parse.cpp` | **wire 直接承载数组**：放行 `Array`（`glkv3_parse.cpp:363` 现 `return -1`）+ 元素必须是 map 的校验 + 深度/长度上限 |
| M2 | `src/core/profile/document.hpp`、`profile/schema.hpp` | **中立 `Document` 复合值承载（本轮最大项）** + `queue` 的 array-of-map 声明与 bind → 有序 `CanonicalPlan` |
| M2 | `src/core/pipeline/component_catalog.hpp`、`orchestrator.hpp` | 计划 → 预设归一化键 → dispatch；**route 进 `CanonicalPlan.route`**；**七个静默点改硬失败 + 具名诊断**（§10.1，尤其 S5） |
| M2 | `src/core/tests/profile_v3_test.cpp` 等 | 每条非法形状一个负例（含 S14a–S14d）；**同形对拍语料**（App 发射 → native 解析 ⇒ 与 HOCON 语义逐字段一致）；token 糖等价对拍 |
| M3 | `app/src/main/assets/kernel_profiles/*.conf`（68 个：58 release + 10 共享） | token → 队列 B 形态（机械改写 + 逐资产等价对拍） |
| M4 | `profile-core/**`、`app/**`、`tools/extract_rs/**` | 队列解析（B）+ **UI 重做**（D4：旧 UI 测试不作验收）+ extractor 产出 B 形态 |
| M5 | 全部 | 删除 token 糖 + 负例 + 文档/UML 同步 |

## 7 数据流/控制流差异（新旧对照 + 不变量）

**旧**：`HOCON steps="<token>"` → App 原样发射 → wire `backend.<id>.steps`(str) → `resolve_combination` 精确解析 → `document.combination` → `dispatch_target_of` → 编译期 `Pipeline` 实例 `run`。

**新**：`HOCON queue=[...]` → App 归一化（或 wire 带队列由 native 归一化，§4.5）→ `CanonicalPlan` → 与 `kVerifiedPresets` 比对 → `supported`（映射回 `CombinationKind` → 同一 `Pipeline`）/`experimental`（具名诊断 + opt-in）→ `Pipeline` 实例内按计划执行步骤（步骤序仍是编译期注册的执行体集合）。

**不变量**
1. **已验证预设执行路径逐字节不变**：`supported` 计划必须映射到与今天相同的 `DispatchTarget` 与 route/terminal；M2–M4 期间用对拍测试保证。
2. **fail-closed 只增不减**：任何非法形状拒绝整个文档，绝不部分执行（沿用 `bind_all` 的"先全量校验再落地"语义）。
3. **wire 数值 id 不变**：`StepSetKind`/`CombinationKind` 的数值与 `vocabulary-manifest.tsv`/`combination-manifest.tsv` 的 `wire` 列保持 1/2/3 与 token 行不变（迁移期），避免跨端大面积重编号。
4. **R1 不变**：`contract/` 仍不 include `backend/pipeline/platform/terminal`；插件映射仍只允许在 `WaiterClosed` 窗口。

## 8 兼容性与回滚

- **兼容（前提：v3 未发布，见 §4.4）**：M2–M4 期间 token 与队列**同时被接受**（token 是糖，解析期展开），但这**只服务仓库内 68 个资产**，不是对外 v3 兼容承诺；**v1/v2 的兼容在 Kotlin 侧**（`LegacyProfileConverter.kt`），native 仍 v3-only ⇒ 不存在「旧外部 v3 文档」这一兼容面。
- **回滚**：每批独立可回滚 —— M2 加的是新键与新校验（旧键路径保留）；M3 是资产改写（`git revert` 即回旧 token 资产）；M5 删糖是**单向门**，因此 M5 必须排在真机门禁与资产全绿之后，并在计划里显式标注"不可与 M4 合并提交"。
- **禁止长期双真相**：M5 有明确的完成判据（token 出现即拒的负例 + 资产 0 命中）。

## 9 验证矩阵

| 批次 | 命令 | 预期 |
|---|---|---|
| M1 / M1.1 | `make -C src native-host-tests -B`、`make -C src lint-tidy`、`ANDROID_NDK_HOME=… make -C src ghostlock -B` | EXIT=0；告警 9（基线）；防火墙 `182 files, 4/4/0/0`（M1 后 181，M1.1 再加 `step_plan.hpp`） |
| M1/M2 | 每条守卫的证伪实验（临时造错 → 失败 → 撤回） | 每条守卫**必须**能失败；证伪一律 `make -B` |
| M2 | 等价性对拍：token 糖 ↔ 队列；**同形对拍**（App 发射的 wire → native 解析 ⇒ 与 HOCON 语义逐字段一致） | `CanonicalPlan` 逐字段相等，0 例外 |
| M2 | manifest 重生成（两份逐字节）+ 防火墙 + **真机门禁**（wire 类型面变更） | **M2 验收判据**：① `supported` 计划与今天**逐字节同路径**（43284 全链 + 43499 冷启）；② 每个 `experimental` 计划**各自**门禁；③ 无队列文档零新增字节；门禁归档 |
| M3 | 逐资产等价对拍（脚本化；**68 个 .conf**） | 58 release + 10 共享全部「计划 == 旧 token 计划」，0 例外 |
| M4 | `./gradlew :profile-core:test :app:testDebugUnitTest`、`make -C src glkv3-golden-hex`（重生成 golden）、`cargo test --release` | 全绿；跨语言 golden 由 native 重出 |
| M5 | 三绿 + 防火墙 + 真机门禁（43284 全链、43499 冷启） | 门禁归档到 `docs/analysis/device-gates/` |

## 10 风险

| # | 风险 | 缓解 |
|---|---|---|
| R1 | **静默弱化**：S1/S3/S5/S6/S7/S10 型的默认值/兜底名让非法计划"看起来跑通了" | 全部改硬失败 + 每条守卫证伪；对照 `f72b145f` 的三条 manifest 守卫 |
| R2 | 归一化漂移（Kotlin 一份、native 一份） | 归一化权威只在一处（native），Kotlin/extractor 用同一份导出语料对拍（沿用 manifest 制度） |
| R3 | **wire 类型面变更**（Array + 复合值）无真机门禁 | **必须真机门禁**（§4.5：supported 逐字节同路径 + 每个 experimental 各自门禁 + 无队列文档零新增字节）；manifest 重生成 + 三绿 + 防火墙 |
| R4 | 实验面被误当 supported（用户以为已验证） | 具名诊断 + App 显式标记 + 缺省不执行（opt-in，U5）+ 升级前必须门禁 |
| R9 | **App 发射与 HOCON 语义漂移**（同形形态下唯一剩下的跨端风险） | §5-Q1 的**同形对拍语料**（硬要求）：wire → native 解析 ⇒ 与 HOCON 语义逐字段一致，进 M1/M2 验收 |
| R11 | **中立 `Document` 复合值承载**（C2）改的是所有 owner 共用的路径，回归面最大 | 限定为**有界**复合值（深度/长度上限、fail-closed）；M2 独立批次、不与其它批次合并；门禁覆盖全部既有 owner 绑定测试 |
| R10 | 加内建步骤的繁琐度引发"退回运行期注册" | §4.6：3 处机械改动 + 单一权威生成 + 脚手架清单 + 错误指路；内建保持编译期注册，插件步骤走运行期注册 |
| R5 | 步骤依赖/顺序规则与攻击语义不符（例如跳过 w1 直接 w2 在真机上必然失败） | 依赖表来自现有 `steps.cpp` 的真实先后（`W1W3Steps/W1W2Steps`）；任何放宽都要真机证据 |
| R6 | 迁移期双真相导致长期分叉 | M5 是计划内必做项，有完成判据 |
| R7 | seam 位置违反 R1（PI waiter 存活期映射插件） | seam 位置在声明期校验 + 负例（S8 类） |
| R8 | **S5 型"静默 Rejected"**：接线缺一条 case，用户看到"什么都没发生"，无法自助定位 | §10.1 的 S5 具名诊断（必须回显归一化计划全字段）+ 测试断言诊断文本 |

### 10.1 静默默认点（S1/S3/S5/S6/S8/S9/S13）+ 新形状守卫（S14a–S14d / S15 / S16–S18）→ 硬失败 + **具名诊断文本**

诊断统一前缀 `plan_error` / `manifest_error`，字段用 `key=value`、可 grep、单行、无分配（沿用 `run.43284` 结构化日志的风格）。**每条都要有证伪实验（`make -B`），且诊断文本进测试断言**（否则文本会漂移）。

| # | 位置 | 现状（静默） | 改法 | 具名诊断（草案；实现可微调但必须保留字段） | 证伪方式 |
|---|---|---|---|---|---|
| **S1** | `identity.hpp:294-300 combination_stepset_wire()` | 未命中 ⇒ 返回 0 | 归一化改为**返回结果类型**（成功/具名错误）；0 只允许来自显式 Unknown 哨兵且调用方必须处理 | `plan_error reason=unknown-step-token backend=<b> token=<raw>` | 造一个不在表内的 token ⇒ 必须打印该行并拒绝 |
| **S3** | `component_catalog.hpp:142-152 combination_terminal()/combination_route()` | `spec==nullptr` ⇒ 默认 RootChild / None | 删掉默认：`nullptr` ⇒ 硬失败（或返回 optional，调用方必须处理） | `plan_error reason=unresolved-combination combination=<id>` | 传入未知 CombinationKind ⇒ 必须失败 |
| **S5** | `component_catalog.hpp:46-65 path_target_of()` + `orchestrator.hpp:83-84` | 未登记计划 ⇒ `DispatchTarget::None` ⇒ **Rejected 且不打印任何东西** | `None` 分支**必须打印归一化计划的全部字段** | `plan_error reason=no-dispatch-target backend=<b> route=<r> steps=[<id,...>] terminal=<t>`（合法但未接线） | 临时注释掉一个 switch case ⇒ 必须打印该行（**本设计最重要的一条证伪**） |
| **S6** | `component_catalog.hpp:96-99 combination_supported()` | 返回 false，调用方各自处理 | 调用方必须把 false 变成诊断（禁止静默 return） | `plan_error reason=unsupported-combination backend=<b> plan=<...>` | 造一个未支持计划 ⇒ 必须打印 |
| **S8** | `component_catalog.hpp:164-172 stepset_name()` | default `"unknown"` | 名字兜底改**硬失败**（导出器守卫） | `manifest_error reason=unmapped-stepset wire=<n>` | 造一个未登记 StepSetKind 值 ⇒ 导出必须失败 |
| **S9** | `identity.hpp:271-278/284-291/305-314 path_name/route_name/backend_token_name` | default `"unknown"`/`"none"` | 同上；导出器加「不得出现 unknown/none」守卫 | `manifest_error reason=unmapped-vocabulary kind=<k> wire=<n>` | 同 S8（path/route/backend 各造一次） |
| **S13** | `combination_manifest_test.cpp` / `vocabulary_manifest_test.cpp` | 无守卫 | 补三条与 profile manifest 同规的守卫：名字兜底（无 unknown/none）、行数 == 各表之和、集合对拍（导出集合 == 编译期权威集合） | 沿用 `profile_manifest_v3_test.cpp` 的 `guard:` 前缀 + 具名 reason | 照 `f72b145f` 的证伪方式（注释掉一张表 / 改一个名字 ⇒ 必须失败） |
| **S14a**（新形状） | route **存在性**（§5-Q2） | 无（新形状） | 有 route 轴的 backend 缺队列级 route ⇒ 拒绝 | `plan_error reason=route-required backend=cve_2026_43499` | 删掉 `route` 字段 ⇒ 必须打印并拒绝 |
| **S14b**（新形状） | route **适用性**（§5-Q2） | 无（新形状） | 无 route 轴的 backend 出现 route ⇒ 拒绝 | `plan_error reason=route-not-applicable backend=cve_2026_43284` | 给 43284 写 route ⇒ 必须打印并拒绝 |
| **S14c**（新形状） | route **唯一性**（§5-Q2） | 无（新形状） | 队列级 route 出现多处 ⇒ 拒绝 | `plan_error reason=route-duplicated` | 重复写队列级 route ⇒ 必须打印并拒绝 |
| **S14d**（新形状） | **每步 route 禁止**（§5-Q2） | 无（新形状） | route 写进数组元素 ⇒ 拒绝 | `plan_error reason=step-route-not-allowed at=<i>` | 造含 route 的步骤元素 ⇒ 必须打印并拒绝 |
| **S15**（新形状） | seam 阶段/位置校验（§5-Q3） | 无（新形状） | 阶段与位置不符、或落在 R1 禁区 ⇒ 拒绝 | `plan_error reason=seam-stage-illegal stage=<s> at=<i> allowed=[pre_spawn,post_terminal]` | 造 `[ {step=w2}, {seam=plugin,stage=pre_spawn} ]` ⇒ 必须打印并拒绝 |
| **S16**（新形状） | 元素形态（§5-Q1，U9） | 无（新形状） | 纯字符串元素 / 非 map 元素 ⇒ 拒绝 | `plan_error reason=queue-element-not-object at=<i>` | 造 `queue = [ "w1" ]` ⇒ 必须打印并拒绝 |
| **S17**（新形状） | 实验面声明（§4.3，U5） | 无（新形状） | 未声明 + 未验证 ⇒ 拒绝；声明了但等于已验证预设 ⇒ 按 supported 跑 | `plan_error reason=experimental-not-declared backend=<b> plan=<...>`；判定行 `plan verdict=supported|experimental` | 造一个未声明的非预设计划 ⇒ 必须打印并拒绝；声明后同一计划必须判 experimental 且可执行 |
| **S18**（新形状） | 预留键（§5-Q1，U10） | 无（新形状） | `params` ⇒ 拒绝，但报 reserved 而非 unknown | `plan_error reason=params-reserved-for-future-step-parameters at=<i>` | 造含 `params` 的元素 ⇒ 必须打印该 reason（不是 unknown） |

**"能失败"是门槛**：上表 15 行（S1/S3/S5/S6/S8/S9/S13 + S14a–S14d + S15 + S16/S17/S18）+ §4.2 非法形状表 11 行 = **26 条守卫**，每条都要在实现批次里给出「造错点 → 失败输出 → 撤回核验」三件套；**证伪一律 `make -B`**。

## 11 裁决（**全部已裁决**，2026-10-06 定稿）

**已裁决（不再是未决项）**
- **U1 = 已裁决（D1）**：队列形态 **B（对象数组）**；A 不保留为 HOCON 语法（建议，见 §5.0）。
- **U2 = 已裁决（D2′，2026-10-06 复裁）**：route **只在队列级**（每步写 ⇒ 拒绝）；**43499 必填 / 43284 不得出现 / 唯一**；真正的「每步换 route」**不做**（独立 L 级 + 真机门禁）。（v2.0 的「每步可写但一致」已被取代，见 §0.2。）
- **U3 = 已裁决（D3）**：seam = **纯占位** + **复用既有插件阶段词汇**（预留标识符）+ R1 位置约束。
- **U4 = 已裁决（Lead 采纳）**：迁移期**严格规范序**（只允许与今天两个 step set 同构的前缀子集，如 `[w1,w2]`），将来按需放宽。
- **U6 = 已定**：资产口径 **68 个 `.conf` = 58 release + 10 共享**；**早前口径 62 是过期计数**（已按 Lead 确认更正）。
- **U7 = 已裁决（Lead 定）**：键名 = **`queue`**（新键；不复用 `steps`，语义不同、显式优于隐式）；`steps` 是否在 M5 移除另议。
- **U8 = 已裁决（Lead 采纳）**：`available{}` **整体判定 + 步骤级 available 仅用于诊断**（避免组合爆炸）。

- **U5 = 已裁决（2026-10-06，按建议）**：实验面 opt-in = **HOCON 静态声明 `experimental`**。配套规则（三条，写进 §4.3）：
  1. **未声明 + 未验证 ⇒ 拒绝**：`plan_error reason=experimental-not-declared backend=<b> plan=<...>`；
  2. **声明了但归一化后等于已验证预设 ⇒ 按 `supported` 跑**——**声明是请求，结论由归一化计算**（禁止"自称 supported"，也禁止把已验证预设降级为 experimental）；
  3. **日志记录判定结果**（每个计划一行：`plan verdict=supported|experimental declared=<0|1> backend=<b> steps=[...] route=<r>`）。
- **U9 = 已裁决（2026-10-06，按建议）**：**A（纯数组）不保留**。唯一合法元素形态 = **对象**；纯字符串元素 ⇒ **拒绝** + 具名诊断 `plan_error reason=queue-element-not-object at=<i>`；可读性交给 **UI 预设按钮**（用户正在重做 UI；预设按钮展开成对象数组写入 profile）。
- **U10 = 已裁决（2026-10-06，按建议）**：`params` **预留但未实现** ⇒ 遇到即拒绝，但报**具名 reserved** 而不是 `unknown`：`plan_error reason=params-reserved-for-future-step-parameters at=<i>`；契约（`step_catalog.hpp` 与 manifest 文档行）写明「**reserved, not implemented**」，将来解开限制即可用，无需改语法。

## 12 明确保留

- `kCombinationCatalog` 12 行与 `combination-manifest.tsv`：迁移期**保留为内部归一化键**（不改数值、不改列义，直到 M5 后的单独批次评估）。
- `StepSetKind{1,2,3}` 与 `vocabulary-manifest.tsv` 的 3 行 stepset：保留（数值不变）。
- `Pipeline` 的编译期 `static_assert` 与 orchestrator 的逐 case 断言：**保留**（队列不改"编译期固定组合"这一原则，只改选择的表达）。
- 插件工程冻结与 `WaiterClosed` 窗口约束：保留；seam 只做惰性占位。
- 攻击原语（waiter/race/route/payload）：不动。
- **不做「每步换 route」**（一次运行多轮 route prepare/disarm + waiter 生命周期重排）——攻击路径改动，需独立 L 级设计 + 真机门禁。
- **不做 A（纯数组）的 HOCON 语法**（§5.0；如需快捷方式，放 UI 层）。
- **保留「wire 与 profile 对齐」原则**：wire 与 HOCON **同形**（对象数组），不做降级映射；「点分索引键」折中**已被用户否决**（沿革见 §0.2/§4.5）。
- **保留**「route 只出现在队列级」（§5-Q2）：位置＝与 `queue` 平级的 backend 字段。
- **不做运行期自适应/规划**（用户前提 ②）：队列是静态声明，计划在解析期完全确定。

## 13 进度

- [x] 设计稿骨架（本文件）
- [x] Lead 首轮评审通过 + 三处修订（§4.5 产品意图对照 / §10.1 具名诊断 / §11 U6 口径）
- [x] **用户裁决并入（2026-10-05，见 §0）**：D1 B 为规范形态；D2 route 每步可写但整条一致；D3 seam 纯占位 + 复用插件阶段标识符；D4 UI 重做；**D5 选 W2**；D6 键名 `queue`
- [x] **用户复裁并入（2026-10-06，见 §0.1 D2′/D5′）**：① route **回到队列级**（43499 必填 / 43284 不得出现 / 每步写 ⇒ 拒绝 / 唯一）；② **坚持改 wire** ⇒ `queue` = 对象数组、HOCON 与 wire **同形**（点分索引折中撤回）；③ §4.5 成本重估（C1–C7 上调，C2 复合值承载为最大项）⇒ **回到独立大批次 + 真机门禁**；④ §5-Q1 语料改为**同形对拍**；⑤ §10.1 扩到 12 行（S14a–S14d），守卫总数 **23**；⑥ §11-U2 更新、U5/U9/U10 仍待用户
- [x] §4.6 正面回答「编译期注册是否更繁琐」（3 处机械改动 + 单一权威生成 + 脚手架；内建编译期 / 插件运行期两套并存）
- [x] 成本重估（v2.1 修正）：C1–C7 按 **wire 直接承载数组** 上调（**C2 复合值承载为最大项**；整体**回到「独立大批次」+ 真机门禁**）
- [x] **v2.2 定稿（2026-10-06）**：U5（HOCON 静态 `experimental` + 三条配套规则）、U9（A 不保留）、U10（`params` 预留未实现）全部按建议落为结论；§11 改为「全部已裁决」；新增 S16/S17/S18（守卫总数 **26**）
- [ ] **Lead 复核 v2.2** → 与 `docs-uml`（契约新章节 + 计划 M1–M5 + UML Class C++）**同批提交**
- [ ] 用户确认遗留项：U5（实验面 opt-in 形态）、U9（A 是否作 App 侧糖）、U10（`params` 预留）
- [x] **M1 已提交 `bcb94253`**（目录/别名/编译期注册 + S5 诊断 + manifest 守卫；14 条证伪证据；防火墙 181/4/4/0/0）
- [x] **M1.1 完成（未提交）**：`contract/step_plan.hpp` 归一化纯函数 + `step_plan_test.cpp`（24 个 reason token 全钉死；20 条证伪证据）；不接生产、行为零变化
- [ ] **vr_guard (a)** 彻底删除 vivo 代码（真机门禁由 Lead 跑）
- [ ] M2（**wire 数组承载 + 中立 Document 复合值** + route 三条守卫 + 具名诊断 + **真机门禁**）/ M3（68 资产）/ M4（UI 重做）/ M5（删糖）

---

**物证索引**：`contract/identity.hpp:54-62/116-214/216-234/251-268/271-300/352-357`；`pipeline/component_catalog.hpp:46-65/69-99/123-152/164-172`；`pipeline/orchestrator.hpp:30-34/44-57/61-86`；`pipeline/pipeline.hpp:53-62`；`backend/cve_2026_43499_backend.hpp:61-62`；`backend/cve_2026_43499/steps.cpp:460/497`；`profile/glkv3_parse.cpp:154-166/207-287/363`；`backend/cve_2026_43499/schema.hpp:88`；`backend/cve_2026_43284/schema.hpp:154`；`app/src/test/resources/{combination,vocabulary}-manifest.tsv`；`app/src/main/assets/kernel_profiles/`（68 `.conf`）；`profile-core/.../Glkv3Encoder.kt:16-36`；`profile-core/.../NativeProfile.kt:258/274/391-407`；`src/core/tests/profile_manifest_v3_test.cpp`（`f72b145f` 三条守卫）。
