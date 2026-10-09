# 设计：能力两份权威的落地（ADR-0012 §4 的两问）· v2

- 状态：**草案 v2**（按 design-critic v0.2 评审修订：Q1-a · Q1-b · Q2 · Q3）
- 日期：2026-10-09 · 基线 `2bb5b950`
- 权威：**ADR-0012**（两处权威 + 对拍三条）；本稿只回答它 §4 留下的两问

## 0 修订（哪条把我改对了）

| 项 | 我原来的说法 | 更正 |
|---|---|---|
| **Q1-a** | 「两级都以**既有词汇**命名（不新起名字）」 | **不实** —— 那三个名字（`collision_search` 等）在 `src/core` **零命中**，是目录/文件名的散文写法 ⇒ **L2 是新词汇**，单处声明 + pin ✓ |
| **Q1-b** | 把「已实现」与「本设备可用」混为一类 | **两类事实**：静态 per backend ✓ × 运行期（设备/route 条件 ✓）⇒ 静态表**不可能**承载运行期结论 ✓ |
| **Q2** | `OpSpec.available` 由能力**单输入**派生 | **单输入会丢掉正当中间态**（「已实现但暂不暴露」是**决策** ✓）⇒ 改为 **`exposed ∧ satisfied` 两输入** ✓ |
| **Q3** | 「`kAvailableRouteMask` 退役（改为导出）」 | **它有 10 处引用、含一处运行期检查** ⇒ **退役需先补四项** ✓，缺一不得退役 ✓ |

## 1 要回答的两问（ADR-0012 §4）

① **能力粒度**：单位与命名是什么？② **四个真实位的去重**：谁保留 · 谁派生 · 谁退役？

## 2 粒度（第一问）

| 级 | 单位 | 命名 | 例 |
|---|---|---|---|
| **L1 效果能力** | 一步要对世界造成的结果 | **既有词汇**：`StepEffect`（`step_catalog.hpp`） | `Selinux` · `Credentials` · `Seccomp` · `PageCache` |
| **L2 原语能力** | 一步要跑起来所需的机制件 | **新词汇（单处声明 + pin）**：名字取**能力名**，**不是**文件路径；实现件路径放**提供表的绑定列**；名称 ↔ 实现件由**断言**绑定（改名即编译失败，照 `kCombinationTokens` 的 pin 先例） | `futex_collision_search` · `kernel_page_prepare` · `arbitrary_write` |

**L2 为何单独成级（Q1-b 更正后）**：提供表是**静态 per backend** 件（实现件路径 + 是否已接线 ✓），而「**本设备能不能用**」是**运行期**事实 ✓ —— 本会话实测：`perf` 路在**这台设备**不投递寄存器组 ✓ ⇒ 同一 backend 在别处结论不同 ✓ ⇒ **两类事实分开**：

- **已实现** = 静态 · per backend（提供表 ✓）；
- **本设备可用** = 运行期事实 · **按 L2 词汇键控** ✓（由设备事实填充 ✓）。

**不**做到 route/链级 ✗（那两级已有权威 `kRouteCatalog` / `kChainCatalog` ✓，再加就是第二处 ✓）。

## 3 四个真实位的去重（第二问）

| 位 | 处置 | 理由 |
|---|---|---|
| `ChainSpec.wired`（`chain_catalog.hpp`） | **保留为权威**（链级接线） | 「这条链端到端通不通」的既有唯一答案 ✓；能力表**不复制** ✓ |
| `OpSpec.available`（`op_registry.hpp`） | **保留，改为两输入派生** ✓：`available := exposed ∧ capabilities_satisfied`（`exposed` = 每 op 的**声明列** ✓ 决策 ✓；`capabilities_satisfied` = **算出** ✓；两列同表并列 ✓，派生由一个 `constexpr` 完成并由**断言**钉住 ✓） | 单输入派生会**丢掉中间态** ✗（链级 `wired` 粒度太粗 ✗）；两输入既保留 `exposed=false` 的正当状态 ✓，又消除手写漂移 ✓（`satisfied` 由能力算出 ✓）；`available` 从此是**派生视图** ✓，不再是权威 ✓ |
| `kCombinations[].wired`（`kind.hpp`） | **保留为权威** ✓ | backend×route 的组合级事实 ✓，粒度不同 ⇒ 不重 ✓；但**对拍**：组合 wired ⊆ 其所需能力的提供 ✓ |
| `kAvailableRouteMask`（`op_registry.hpp`） | **退役需先补四项** ✓（见下） | 实测**全树 10 处引用**（定义 `:64` ✓ · 3 个 op 行 `:99/:101/:107` ✓ · 两条断言 `:147`/`:199` ✓ · **运行期** `plan_gate.hpp:214-215` ✓ · 3 处测试含 `plan_gate_test.cpp:143` 的 **widen 越权用例** ✓）⇒ 退役不只删常量 ✓：gate 的**第二半检查**（不得越出本构建可用 routes ✓）需要新答案 ✓ |
| `PlanInput.capability_present`（`plan_gate.hpp`，**无生产者**） | **退役，但必须替换** ✓ | 它是 `CapabilityMissing` 的**消费点**之一 ✓ ⇒ 直接删会让该拒绝**失去触发源** ✗ ⇒ 替换为**由设备事实填充、以 L2 词汇为键的运行期能力输入** ✓（拒绝码仍是 `capability-missing` ✓） |

**`kAvailableRouteMask` 退役的四项前置** ✓：① **替代来源（R1 已定，写成派生规则）**：**可驱动 route 集合 = `kCombinations[].wired` 的投影** ✓（ADR-0012 §3 已把它保留为权威 ✓，且它是 per（backend,route）事实 ⇒ 投影即构建级答案 ✓）；**不能**用 `kRouteCatalog` ✗ —— 实测其行结构 `RouteCatalogEntry { token, wire }` **没有 wired/available 位** ✓，表头注释自称「The **wired** routes」✗ ⇒ 「全表 = 可驱动集合」**今天只是偶然成立** ✗；一旦出现「能解析但不能驱动」的 route，该目录**无法表达** ✗。**前置**：把该投影与 `kChainCatalog[].wired` 的**逐行对拍**先补上（评审指出此项未核 ✓）。② 3 个 op 行的掩码来源 ✓；③ 两条断言重瞄 ✓；④ gate 的 `:214-215` 检查与 `:143` 用例**重写并保留可失败性** ✓（证伪：构造越权掩码 ⇒ **必须红** ✓）。**缺任一项不得退役** ✓。

**净效果**：能回答「这个 backend 提供了什么」的地方 **7 → 2** ✓（能力目录 + 提供表 ✓）+ 1 处组合级 ✓ + 1 处链级 ✓（粒度不同且各有对拍 ✓）。

> 三输入判定表、以及 L2 名单 + 绑定表：见同目录 `capability-authorities-tables.md`。

## 4 两个文件与对拍

- `contract/capability_catalog.hpp`（上层包一处）：能力名（L1 用 `StepEffect` ✓；L2 用新词汇能力名 ✓）· 语义一句 · **必需/可选**（数据 ✓）· 编译期自洽断言；
- `backend/<id>/capabilities.hpp`（每个 backend 一处）：能力名 → **实现件（绑定列）+ 是否已实现**；
- 对拍（host 测试，三条 + 证伪 ✓）：① **声明 ⊆ 提供** ✓（缺一 ⇒ 具名拒绝 ✓）；② **提供 \ 声明 为空或逐条登记** ✓（照 R1 的**空账本 + pin** ✓，stale 即 FAIL ✓）；③ **必需/可选是数据** ✓（可选缺失在选择时**具名拒绝** ✓）；④ **证伪** ✓（去掉一项提供 ⇒ **必须红** ✓）。

## 5 本稿不覆盖（不猜）

- `requires_caps` 散文串的处置（改机器可读 / 删除 ✓）—— 属落地实施批 ✓（ADR-0012 §3 已给二选一 ✓）；
- 能力表与 manifest（`available` 列 ✓）的导出关系 —— 落地时定 ✓；
- **Kotlin 侧**是否已有能力枚举 ✗ —— 未核 ✓（对方车道勘察 ✓）；
- ~~`kRouteCatalog` 能否单凭自身回答「本构建能驱动哪些 route」~~ —— **已核：不能** ✓（`RouteCatalogEntry` 无 wired/available 位 ✗；「全表 = 可驱动」只是偶然成立 ✗）⇒ 替代来源见 §3 的 ① ✓。
