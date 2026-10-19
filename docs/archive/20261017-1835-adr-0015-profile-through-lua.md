> 归档说明（2026-10-17）：归档原因 = **ADR 废除**（Lua / script / JCS 载体已删除，见 docs/plan/MASTER-PLAN.md §0.1）。原始路径 = docs/analysis/adr/0015-profile-through-lua.md；归档路径 = docs/archive/20261017-1835-adr-0015-profile-through-lua.md。

# ADR-0015：profile 解析结果一律经 Lua 中转，native 不直接转执行层

> **已废除（2026-10-17 用户裁定）**：vendored Lua / mpack / nlohmann 与 GLKv3 文档路径已从构建和源码树删除，
> 本 ADR 的载体（Lua 沙箱、脚本面、文档解析）不再存在；保留此文仅为历史记录，现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

- 状态：**Accepted**（**用户裁定** 2026-10-09）
- 日期：2026-10-09 · 基线 `2b5f8000`
- 沿革：用户原话「解析了 profile 全部交给 lua 中转，在 native 里不直接转到执行层」

## 1 裁定

1. **native 的 profile 解析只产出数据** —— 解析完之后，**结果交给 Lua**（成为脚本环境里的值/表）；
2. **执行层不直接读 profile** —— 偏移 · route 选择 · 尺寸 · 步骤数据，凡执行时要用的，**都从 Lua 侧传下来**；
3. **Lua 成为唯一中转** ⇒ 同一份事实**只有一条**到达执行层的路（native → Lua → 执行层）。

## 2 为什么（用户的取向，如实记录）

- 与 ADR-0014 同源：**执行层只做原语**（按地址读写），**策略与参数集中在脚本** ⇒ 换设备/换 route/换偏移 = **改脚本**，不改 native；
- 于是 native 的 profile 面**收窄为解析器**：解析 · 校验 · 交给 Lua · 结束 ⇒ 不再持有执行期该用什么的知识；
- 好处：**一处决策**（脚本）· **设备差异不进 native** · 与「顺序归 Lua」（ADR-0011）同一方向。

## 3 取代/收紧什么（不删历史）

| 既有件 | 处置 |
|---|---|
| **ADR-0011 的 plan 通道**（`PlanOp` / `staged_plan` / `plan_gate` / `plan_delivery`） | **收紧**：计划**不再**由 native 侧 profile 直接生成 ⇒ 「谁产计划」那一半被本 ADR 取代；「**顺序归 Lua**」那一半**仍成立**（且更强：参数也归 Lua） |
| `profile/glkv3_parse.cpp` 的执行侧读取 | **退役**（解析仍在，**执行期不再读它**） |
| backend 侧对 profile 的取值（偏移 · 调参） | 改为**从 Lua 传入**（`AccessParams.offset` 正是这个形状） |
| `profile/key_catalog.hpp` + manifest 面 | **保留**（解析与校验仍需要它；它描述的是 **wire 形状**，不是执行决策） |

## 4 落地形状（动工前出设计稿 + 评审）

1. **native → Lua 的传值面**：解析结果以什么形态交给 Lua（整表一次给，还是逐键调用）；
2. **Lua → 执行层的调用面**：脚本用这些值去调 `KernelAccess`（route · 偏移）与工具函数（上层步骤）；
3. **fail-closed 边界**：**脚本没传**的必需值（如某次写缺 offset）⇒ 按名拒绝（`BadParameters`/`NotInitialized`），**不得回退到 profile**；
4. **与 ADR-0014 的关系**：`KernelAccess` 是**唯一**执行面 ⇒ profile 的值只能经 Lua 变成它的实参。

## 5 未定（不猜）

- **解析到哪一层为止**：native 只做 wire 解析（MessagePack → 键值），还是连 owner 段/类型校验也留在 native —— 设计稿定；
- **Lua 侧拿到的形态**（整表一次给，还是按需查询）与**是否可缓存**；
- **路由/终端的选择是不是也从 Lua 来**（若也归 Lua，则 `available`/`supported` 门禁的位置要重新定位）—— 设计稿必答；
- **安全后果**：Lua 成为**唯一**能决定「写哪里、写什么」的地方 ⇒ 脚本沙箱（可见面 · 资源限制 · 失败语义）成为**关键件** ⇒ 设计稿必须含它。
