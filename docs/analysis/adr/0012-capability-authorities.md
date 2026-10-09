# ADR-0012：接口声明能力与 backend 提供能力各有一处权威文件 · v2

- 状态：**Accepted**（**用户裁定** 2026-10-09）；本版按 design-critic v0.2 评审修订（P1–P4）
- 日期：2026-10-09
- 沿革：由用户在同批提出（原话：接口声明能力和 backend 提供能力应该在上层包和各 backend 下有一个权威文件）；与 ADR-0004（组件模型）、ADR-0006（terminal 归属）同族

## 1 裁定

**两个事实、两个权威文件**：

| 事实 | 权威文件（位置） | 内容 |
|---|---|---|
| **接口声明能力**（上层包对世界的承诺） | **上层包**（`core/src/contract/`）一处 | 该接口**声明**要求各 backend 提供什么（能力名 · 语义 · 必需性） |
| **backend 提供能力**（各 backend 实际给了什么） | **每个 backend 各一处**（`core/src/backend/<id>/`） | 该 backend **提供**了哪些能力（能力名 → 实现件路径 · 是否已接线） |

**对拍三条（P2 更正：不是「双向相等」）** —— 要求相等会把本项目正当的中间态逼成红（例如 `kChainCatalog` 十二行里五行 `wired=false`），产生「为过测试而提前暴露」的反向压力：

1. **声明 ⊆ 提供**（强制）：目录声明了却无人提供 ⇒ **具名拒绝**；
2. **提供 \ 声明 必须为空或逐条登记**：形态照 R1 include 防火墙的「**空账本 + pin**」—— 未登记能力 ⇒ FAIL，账本条目 stale ⇒ 同样 FAIL；
3. **必需/可选是目录里的数据标记**：可选缺失**不得静默**，而是在具体 backend 上被选择时按既有码族**具名拒绝**。

## 2 为什么需要（前提成立，措辞已按 P1 更正）

1. **本会话已连着吃到同类亏**：步集/词汇面**手抄镜像且无人对拍** ⇒ 改名后产物与枚举各说各话而无红；
2. **能力事实散落在多处，且无处能按 backend 枚举能力名**（评审逐个核过，共 7 处）：
   `ComponentEntry{kind,token,selectable}`（`kind.hpp`，**组件级**）· `kCombinations[].wired`（**backend×route**）· `ChainSpec.wired`（`chain_catalog.hpp`，**链级**）· `OpSpec.available`（`op_registry.hpp`，**op 级**）· `kAvailableRouteMask`（**构建级、非 per-backend**）· `PlanInput.capability_present`（`plan_gate.hpp`，**一个布尔且 `core/src` 内无生产者**）· 各 manifest 的 `available` 列（上述的导出镜像）；
3. ADR-0004/0006 已立「identity 是组合唯一权威」⇒ 本 ADR 把**能力**这一面补上同构的一条。

**P1 更正（撤回一处幻影字段）**：初稿写的「`OpSpec` 的 `armed/wired` 两位」与事实不符 —— `OpSpec` 只有 `available` 一个接线布尔，**没有 `armed`**；全仓检索 `armed` 只命中测试里的 *armed addresses*（与本议题无关）。⇒ 去重对象以上面**真实的位**为准。

## 3 形态（落地时细化）

**能力名的基座（P3）**：复用既有的语义词汇 **`StepEffect`**（`Selinux` / `Credentials` / `Seccomp` / `PageCache`，`step_catalog.hpp`）作为能力名基座；需要时再补 route/terminal 类能力。
**不得另起一套名字**（那正是本 ADR 要治的病）。并同批处理一处**空头承诺**：`OpSpec.requires_caps` 现为散文串（`selinux+cred+seccomp` 等），其注释声称事实来自 **`contract::Capabilities` —— 该类型不存在**（全仓仅此注释提及）⇒ 二选一：**改为引用上述词汇（机器可读）**，或**删除该字段并写明由谁替代**。

其余形态：

- 上层包：一份**能力目录** —— 能力名 · 语义一句 · **必需/可选**（数据）+ **编译期断言**（目录自洽）；
- 各 backend：一份**提供表**（`backend/<id>/capabilities.hpp`）—— 能力名 → **实现件路径 + 是否已接线**；
- **对拍**（host 测试）：§1 的三条 + **证伪** —— 去掉一项提供 ⇒ **必须红**；
- **与既有件的关系**：`kComponents` / `kChainCatalog` / `kOpCatalog` **不撤**（各是词汇/链/op 的权威）；本 ADR 只新增**能力**这一面的两处权威，落地时把上面那些位与之**对齐**（谁保留 · 谁派生 · 谁退役）。

## 4 未定 —— 每项都有落地稿指针（P4）

| 未定项 | 落地稿（**动工前必过评审**） | 该稿必须回答的最小问题 |
|---|---|---|
| **能力粒度** | 能力两份权威的落地设计稿 | 粒度的**单位与命名**（本 ADR 倾向「步骤 + 数据面原语」两级） |
| **四个真实位的去重** | 同上 | 谁保留 · 谁派生 · 谁退役（`kCombinations[].wired` · `ChainSpec.wired` · `OpSpec.available` · `kAvailableRouteMask` 及无生产者的 `PlanInput.capability_present`） |

⇒ 二者**必须在动工前定**（粒度一旦落地即成两侧对拍物，改口径＝返工 + 重冻 + 沿革；去重不定就会出现第三处能回答同一问题）；可迭代的只是**实现范围与接入顺序**。
