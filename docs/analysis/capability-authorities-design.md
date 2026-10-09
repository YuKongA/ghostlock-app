# 设计：能力两份权威的落地（ADR-0012 §4 的两问）

- 状态：**草案**（动工前必过评审 · design-critic）
- 日期：2026-10-09 · 基线 `16187f15`
- 权威：**ADR-0012**（两处权威 + 对拍三条）；本稿只回答它 §4 留下的两问

## 1 要回答的两问（ADR-0012 §4）

① **能力粒度**：单位与命名是什么？
② **四个真实位的去重**：`kCombinations[].wired` · `ChainSpec.wired` · `OpSpec.available` · `kAvailableRouteMask`（+ 无生产者的 `PlanInput.capability_present`）—— 谁保留 · 谁派生 · 谁退役？

## 2 粒度（第一问）

**两级，且两级都以既有词汇命名（不新起名字）**：

| 级 | 单位 | 命名基座 | 例 |
|---|---|---|---|
| **L1 效果能力** | 一步要对世界造成的结果 | **`StepEffect`**（`step_catalog.hpp`，既有） | `Selinux` · `Credentials` · `Seccomp` · `PageCache` |
| **L2 原语能力** | 一步要跑起来所需的机制件 | 既有模块名（`leak/` · `memory/` · `race/` 的各件名） | `collision_search` · `page_prepare` · `write_primitive` |

**理由**：① L1 已经存在且**正是**用户口中的三步（`disenforce_selinux` → `Selinux` …）⇒ 不新增词汇；
② L2 是「提供」侧真正会缺的东西（本会话实测：`perf` 路在本设备**不投递寄存器组** ⇒ 该 backend 的某原语**不可用**）⇒ 能力表必须能表达这种**部分可用**；
③ 两级之外一律不要（**不**做到 route 级或链级 —— 那两级已有权威 `kRouteCatalog` / `kChainCatalog`，再加就是第二处）。

## 3 四个真实位的去重（第二问）

| 位 | 处置 | 理由 |
|---|---|---|
| `ChainSpec.wired`（`chain_catalog.hpp`） | **保留为权威**（链级接线） | 它是「这条链端到端通不通」的既有唯一答案；能力表**不复制**它 |
| `OpSpec.available`（`op_registry.hpp`） | **保留，但改为派生**：由「该 op 所需能力（L1+L2）在该 backend 上是否齐备」**算出** | 今天它手写 ⇒ 与能力表**必然**会漂（本会话已见过两次同类漂移） |
| `kCombinations[].wired`（`kind.hpp`） | **保留为权威** | 它是 backend×route 的组合级事实，粒度与能力表不同 ⇒ 不重；但要**对拍**：组合的 wired 必须 ⊆ 其所需能力的提供 |
| `kAvailableRouteMask`（`op_registry.hpp`） | **退役**（改为由 route 目录 + 各 backend 的提供表**导出**） | 它是**构建级**掩码，与 per-backend 能力表是同一事实的另一种问法 ⇒ 两处能答＝本 ADR 要治的病 |
| `PlanInput.capability_present`（`plan_gate.hpp`，**无生产者**） | **退役**（或改为消费能力表） | 一个没有生产者的布尔 —— 要么是死件，要么该由提供表填；倾向**删除并写明**由能力表替代 |

**净效果**：能回答「这个 backend 提供了什么」的地方从 **7 处**收到 **2 处**（能力目录 + 提供表）+ 1 处组合级（`kCombinations.wired`）+ 1 处链级（`ChainSpec.wired`）—— 后两处粒度不同且各有对拍。

## 4 两个文件与对拍（形态）

- `contract/capability_catalog.hpp`（上层包一处）：能力名（L1 用 `StepEffect`，L2 用模块名）· 语义一句 · **必需/可选**（数据）· 编译期自洽断言；
- `backend/<id>/capabilities.hpp`（每个 backend 一处）：能力名 → **实现件路径 + 是否已接线**；
- 对拍（host 测试，三条 + 证伪）：
  1. **声明 ⊆ 提供**（缺一 ⇒ 具名拒绝）；
  2. **提供 \ 声明 为空或逐条登记**（照 R1 的**空账本 + pin**，stale 即 FAIL）；
  3. **必需/可选是数据**（可选缺失在选择时**具名拒绝**，不静默）；
  4. **证伪**：去掉提供表里的一项 ⇒ 对拍**必须红**（否则是 vacuous）。

## 5 本稿不覆盖（写明不猜）

- `requires_caps` 散文串的处置（改机器可读 / 删除）—— 属**落地实施**批（ADR-0012 §3 已给二选一），执行时同批做；
- 能力表与 **manifest**（`available` 列）的导出关系 —— 落地时决定谁导出谁（现有 manifest 模式已够）；
- **Kotlin 侧**是否已有能力枚举 —— 未核，属对方车道勘察。
