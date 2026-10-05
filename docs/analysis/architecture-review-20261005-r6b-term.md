# 架构审查：R6b/T5（组合 token）+ ADR-0006 T1–T4（terminal 归属）

- 日期：2026-10-05　审查人：Lead　快照：**工作树未提交状态**（R6b/T5 半落地 + TERM T1–T4 已实现）
- 基准：`docs/development/design-philosophy.md`（11 条原则）、`engineering-standards.md`（§2 架构 / §3 数据流 / §4 控制流 / §5 数据结构）、
  ADR-0001（允许依赖图）、ADR-0004（组合轴）、ADR-0006（terminal 归属）
- 取证：`make -C src native-host-tests` EXIT=0（防火墙 `173 files, 4/4/0/0`，61 项 ok/OK）；`git status` 30 文件 / `+661 -360`

## 1. 结构清单（逐符号：功能 / 作用 / 影响 / 兼容性）

| 符号 | 功能与作用 | 影响面 | 兼容性 |
|---|---|---|---|
| `contract::CombinationKind`（13 值 = Unknown + 12 token） | 组合的**紧凑 id**；每 token 一个枚举值 | 加 backend/route/path 都会**线性增长**（见 F1） | native 内部，无外部 ABI 承诺 |
| `contract::CombinationSpec{backend,kind,token,route,steps,terminal,available}` | **单一权威**：token → 四元组 + 可用性 | 被 catalog/后端/Kotlin 对拍消费 | 有 **10 B padding**（F2），卡 lint |
| `kCombinationCatalog[]`（12 项） | 白名单：6 可用 + 6 计划（`available=false`） | 未知 token 拒绝的依据 | 与 R6b 设计矩阵一致 ✓ |
| `combination_spec/resolve/name/available/stepset_wire` | 查表/解析/命名/可用性/旧 wire id 映射 | 解析期与选择门禁 | 旧 uint（1/2/3）→ token 映射在此 |
| `backend_kind_from_token` / `terminal_kind_from_token` | 文档根 token → kind | 绑定入口 | 保留旧值 ✓ |
| `pipeline::dispatch_target_of`（3 个重载）/ `path_target_of` / `combination_supported` | 由组合解析到编译期 `DispatchTarget` | 每个 case 用 `Pipeline::target = path_target_of(...)` static_assert 锁定 | 旧的 `(backend,steps,terminal)` 语义被 token 取代（需确认无残留） |
| `Pipeline::target`（static constexpr） | 编译期锁定装配 | 新增组合必须显式登记 | ✓ 符合「能编译期决定的不留给运行期」 |
| `profile::Document::Value::is_text/text`、`ReadResult::storage` | string 值 + **缓冲所有权**（防悬垂） | R4 引入、R6b 复用 | wire `str` ≤256 B，canonical 不变 ✓ |
| 两后端 `schema.hpp`/`glkv3_schema.hpp` 的 `steps` | uint → **string token** | manifest 两行类型变化 + 金标重算 | 旧 uint 兼容映射（F10） |
| TERM：`terminal/root_script.*`（新增 `bool safe_mode` 参数） | 切断 terminal → 43499 state 依赖 | 脚本文本**逐字节不变**（sha256 证据） | ✓ 零行为变化 |
| TERM：`backend/cve_2026_43499/terminal/root_child.cpp`（移动） | 43499 专用 terminal 实现下沉 | Makefile 路径、防火墙计数 170→173 | 命名空间**未搬**（F5） |
| TERM：`backend/cve_2026_43284/entry.{hpp,cpp}` | 入口缝合（`read_side_channel`/`bind`/hooks） | `main.cpp` 直连 backend include **4 → 1** | 无新增堆分配（对 spray 关键）✓ |

## 2. 发现（Findings）

### F1（中）`CombinationKind` 是「backend × route × path」的扁平乘积 —— 扩展性隐患
**证据**：13 个枚举值里 9 个属于 43499（3 route × 3 path）、3 个属于 43284。
**影响**：册中还有 4 个占位 backend（`43503`/`31431`/`64560`/`23274`）。若它们各自带 route 轴，枚举将**按 backend 数线性膨胀**（每个再加 3–9 值），且 `kCombinationCatalog` 同步膨胀。
**与哲学冲突**：原则 2「信息隐藏与契约：显式优于隐式」+ 原则 3「每个事实只有一个权威」——**组合维度**应被分解，而不是把笛卡尔积枚举化。
**建议**：`CombinationSpec` 保留（它是权威表），但把 kind 分解为 `{BackendKind, RouteKind|None, PathKind}`（或每 backend 一张二维表），枚举只保留 **path** 词汇（`rootchild/shizuku/umh`）。

### F2（中）`CombinationSpec` 字段序 → 10 B padding，**卡住 lint**
**证据**：`make -C src lint-tidy` EXIT=2，10 条 `clang-analyzer-optin.performance.Padding` 全在 `contract/identity.hpp:129`（`-warnings-as-errors`）。
**影响**：门禁红线（lint 必须 0）；两个流都被它挡住（TERM 报告其自身文件 0 findings）。
**建议**：字段按大小降序重排（16 B 的 `string_view` 靠前，`bool available` 与其它小字段相邻），或显式补齐并注释理由。

### F3（中）`RouteKind::Auto` 被复用为「无 route 轴」哨兵
**证据**：`kCombinationCatalog` 中 43284 的三条都是 `RouteKind::Auto`；`contract/model.hpp:258` 仍以 `Auto` 作缺省。
**影响**：`Auto` 原语义是「按几何**推断**」（R6b 明确要删除推断），现在又承担「该 backend 没有 route 轴」，**一符两义**；将来 43284 加 route 时无法区分「未选择」与「无此轴」。
**兼容性**：`kRouteAuto` 仍被 v1/v2 遗留映射引用 → 不能直接删，但应**停止复用**。
**建议**：新增 `RouteKind::None`（或 `std::optional<RouteKind>`）表示「无 route 轴」；`Auto` 仅保留给遗留解码路径，并在文档中标注 deprecated。

### F4（**高**）Kotlin 侧尚无 token 表：白名单与可用性**只存在于 native**
**证据**：`grep -rn "mcast_rootchild|combination" profile-core/src/main app/src/main` → **无命中**（只匹配到无关的 XzDecoder 注释）。
**影响**：UI 的「推荐/备选/计划项置灰」若硬编码，就违反了本项目既有约定（`profile-manifest-v3.tsv` = native 导出 + Kotlin 对拍 + agreement test）。一旦 native 增删 token，App 会**静默漂移**。
**与哲学冲突**：原则 3（单一权威）+ 原则 10（机制防错，而非记忆防错）+ 工程规范「双侧一致性（改了必须两边同步，测试会抓）」。
**建议**：像 manifest 一样**导出 token 表**（`owner/token/backend/route/steps/terminal/available`），Kotlin 读它渲染下拉并加 `CombinationTokenAgreementTest`。**这是 R6b 落地前的设计缺口，不是实现细节。**

### F5（中）TERM T2 的声明/实现分居（过渡态未收尾）
**证据**：`terminal/root_child.hpp`（声明，命名空间 `ghostlock::terminal`）留在中性层，实现已移至 `backend/cve_2026_43499/terminal/root_child.cpp`；原因是 `pipeline/orchestrator.hpp` 仍需 include 该头。
**影响**：这是**最难维护的中间态**（头在中性层、实现在 backend，读者会误判依赖方向）；ADR-0006 的目标是「实现下放」，但声明不下放会长期保留 `terminal → (无 backend)` 的假象。
**建议**：R6b 既然已重写 `pipeline/orchestrator.hpp`，应**同批收尾**（1 处 include + 2 处限定名），或立即排 T5 尾巴。

### F6（低）「选择词汇」住在容器层：`contract` → `profile::RouteKind`
**证据**：`contract/identity.hpp` 的 `CombinationSpec.route` 用 `profile::RouteKind`。
**影响**：ADR-0001 允许该边（profile 可被所有层只读），但**语义**上「route 选择词汇」属于 `contract`；`profile` 是容器（document 解码结果）。
**建议**：R5 清理时把 route/path 词汇上移 `contract`，`profile` 仅保留解码用的镜像。

### F7（过程，中-高）两条流并发改同一工作树，产生不可编译中间态
**证据**：R6b/T5 与 TERM 同时进行 → `src/core/tests/profile_entry_test.cpp:105` 一度引用 `contract::CombinationKind` 而 host 编译失败（EXIT=2），lint 亦被 F2 阻塞；两方均报告「对方的半落地状态」。
**与哲学冲突**：原则 5（增量交付与变更控制：未列入清单的代码即冻结）+ 工程规范「写范围不重叠 / 门禁运行期间不得改被该门禁覆盖的源文件」。
**建议**：（a）本批**原子提交**（R6b 全部文件一次进 git）；（b）TERM 的改动**先落成 patch/分支**再合并；（c）同一时刻只允许**一条写入流**。

### F8（低）`StepSetKind` 成为 token 的派生量，但仍可直接选择
**证据**：`CombinationSpec.steps` 承载 `W1W2/W1W3/PageCacheWrite`；`combination_stepset_wire` 仍输出旧 uint。
**影响**：与「`w1_w2/w1_w3` 不再是用户可选值」的设计一致，但类型上仍可被直接使用 → 未来可能绕过 token。
**建议**：在文档/注释中标为**内部派生**；R5 收窄其可见性（或仅在兼容映射里出现）。

## 3. 兼容性矩阵（本批影响面）

| 面 | 变化 | 迁移 |
|---|---|---|
| wire `backend.<id>.steps` | uint → **string token** | 旧 uint（1/2/3）→ 等价 token + 诊断 `legacy_steps_id` |
| wire 根 `selection`（R3 引入） | 顶层 `steps` 取消，`terminal` 降为一致性校验 | HOCON 同步；不一致即拒绝 |
| HOCON | `selection.steps` → `backend.<id>.steps` | 解析期归一（别名） |
| manifest | 两行类型 uint→str；字段数不变 | 两份资源重生成 + 对拍 |
| GLKv3 金标 | 值变化 → 哈希重算 | 须做一次**非自证**比对（用 native hex 输出） |
| 旧导出 bin | 被 fail-closed 拒绝（R6a 教训） | 门禁必须用当批导出文档 |
| Kotlin UI | 双下拉 → 单下拉（推荐/备选/计划项置灰） | 需 F4 的 token 表 |
| extractor | 产出 token | 生成 ≡ 内置 测试同步 |

## 4. 与设计哲学逐条对照

| 原则 | 本批表现 |
|---|---|
| 1 真机是唯一裁判 | **待补**：3 route 真机门禁与 43284 `umh` 尚未跑 |
| 2 显式优于隐式 | `available=false` 显式登记 ✓；unknown token 拒绝 ✓；但 `Auto` 哨兵与扁平枚举是隐式（F1/F3） |
| 3 单一权威 | native 侧 ✓（`kCombinationCatalog`）；**跨语言 ✗**（F4） |
| 4 编译期优先 | `Pipeline::target` static_assert + constexpr 白名单 ✓ |
| 5 增量与冻结 | ✗ 半落地 + 双流并发（F7） |
| 6 证据优于断言 | TERM 的 sha256 对照 ✓ 范本；R6b 尚缺真机证据 |
| 7 无指责复盘 | 本文件即复盘；两条流都如实报告了对方的中间态 ✓ |
| 8 防御式 | 未知前缀/token 拒绝 ✓；`ReadResult` 持有缓冲防悬垂 ✓ |
| 9 决策记录 | ADR-0006 ✓；R6b 设计 v2 ✓；但 F3/F4 未进设计 |
| 10 机制防错 | 缺 Kotlin 对拍机制（F4） |
| 11 职业实践 | 子代理报告质量高（含偏差与依赖说明）✓ |

## 5. 结论与建议顺序（**思想先行**）

**结论**：R6b/T5 的**方向正确**（token 单一权威、编译期锁定、计划项显式），但**结构上有三处需要先补设计再落地**：F1（组合维度分解）、F3（`Auto` 双关）、F4（跨语言 token 表导出）；两处工程性问题：F2（padding 卡 lint）、F7（并发半落地）。

**建议顺序**：
1. **补设计**（文档）：F1 的组合维度分解方案、F3 的 `None`/optional 语义、F4 的 token 表导出格式（新增 `--export-combinations` 或并入 manifest）；
2. **修结构**：F2 字段重排（解 lint 红线）、F3 落地 `None`；
3. **补 Kotlin 侧**（F4）：读导出表 → 单下拉（推荐/备选/计划项置灰）+ agreement test；
4. **收尾 TERM T2**（F5）：`root_child.hpp` 随 R6b 的 orchestrator 一起搬；
5. **原子提交**（F7）：一个提交落地 R6b/T5；TERM 改动随后单独提交；
6. **门禁**：host/NDK/lint/Kotlin/cargo + **真机 `mcast_rootchild`/`pselect_rootchild`/`tcp_rootchild` 各一次 + 43284 `umh`** + AVB。
