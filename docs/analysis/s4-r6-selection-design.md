# S4 · R6 实施设计：选择语义重构（**fallback 出栈** + **3 个 route 并入 step 自由组合**）

> 维护者决策（2026-10-05）：「**以后 fallback 不要传到 native 里去了**，在 Kotlin 界面用户将看到推荐方案，并提供备选方案可选，传到 wire 里只留一个用户选的；
> 43499 层面 route 也是；**3 个 43499 直接集成进 step 里，通过自由组合**。」
> 前置：R3（HOCON 布局）与 R4（string 策略）已完成/进行中。

## 1. 取证（现状）

- native 的 fallback **只在一处**生效：`backend/cve_2026_43499/route/route_policy.hpp`（`run_route_with_fallback`：主 route 失败且 `allow_fallback` 时按 `profile.fallback_route()` 换一条）；
  数据来源 `contract/model.hpp` 的 `meta.fallback_route`（缺省 = `RouteKind::Auto`）。
- 资产分布：**41 个 `fallback.to = "none"`、17 个 `fallback.to = "select_stack"`** → 删除 fallback 会让这 17 个 profile **少一次自动重试**（有意为之）。
- route 目前**不是**装配轴：`component_catalog.hpp` 的装配是 `backend × steps(W1W2/W1W3/PageCacheWrite)`，route 是 `MiddlewareKind`（`RouteKind{Auto,TcpZerocopy,SelectStack,MulticastWaiter}`）。

## 2. 目标语义

1. **native 只收到一个选择**：没有 fallback、没有 `Auto` 推断；失败即失败（错误可见）。
2. **route 并入 step**：选择由 **组合 token** 表达 —— `selection.steps = "<route>_<wset>"`，例如
   `multicast_waiter_w1_w3`、`select_stack_w1_w3`、`tcp_zerocopy_w1_w3`、`multicast_waiter_w1_w2` …；
   **白名单 = catalog**（未知 token → 绑定期拒绝，fail-closed）。
3. **UI 出推荐 + 备选**：Kotlin 依据设备事实/profile 几何给出**推荐组合**，并列出**备选组合**；用户选定的那一个才进 wire。
4. `backend.cve_2026_43499.route.<kind>.*` 只保留**所选 route** 的几何（R2 已 owner-qualified）。

## 3. 逐层改动

**wire/HOCON**
- 删除 `common.fallback_route`（native `meta.fallback_route` 随之删除）；
- `selection.steps` 语义改为组合 token（string，R4 的 string 类型已就绪）；HOCON 同步（R3 布局里 `selection.steps`）；

**native**
- 删 `contract/model.hpp` 的 `meta.fallback_route` / `fallback_route()` / `RouteKind::Auto` 的推断使用点；
- 删 `route_policy.hpp` 的 fallback 分支与 `RouteRunResult::fallback_used`；
- `contract::StepSetKind` 扩为组合枚举（或 `route × wset` 的 token 表）；`component_catalog` 为每个组合登记 triple 并 `Pipeline::target` static_assert 锁定；route 由 token 派生（不再独立 middleware 选择）；

**Kotlin/UI**
- 删除 fallback 选择器与其 UI 状态（`GhostlockViewModel` 的 `profileFallback` 等）；
- 新增「**推荐方案 / 备选方案**」选择器：推荐由设备事实 + profile 几何计算（单一决策点，靠近 `ExecutionModeMapping`）；
- 只把用户选定的组合写入 wire（`selection.steps`）；

**extractor**
- 产出新布局时不再写 `fallback`；其 `flatten(生成) ≡ flatten(内置)` 测试同步。

## 4. 门禁

| 门槛 | 判据 |
|---|---|
| host/NDK/lint | 全绿；catalog/token 白名单用例（未知 token 拒绝） |
| Kotlin | 新旧布局、推荐/备选选择、wire 只含一个组合 |
| **真机 route 自由组合** | **3 条 route 各跑一次**（multicast / select / tcp，若某 route 在本机几何不可用则记为不可用而非失败） |
| **无 fallback 行为** | 构造主 route 必失败的输入 → **直接失败且错误可见**（不再自动换 route） |
| 43499 常规回归 | 推荐组合下与 R4 取值一致 |

## 5. 待确认（1 项）

**组合的表达方式**：推荐**组合 token 字符串**（`multicast_waiter_w1_w3`）——零新增 wire 类型、catalog 白名单校验、UI 直接选；
替代方案是数组（`steps = ["multicast_waiter","w1","w3"]`），但需要给 wire 增加 **array 类型**（R1 的 `wire` 词汇目前只有 uint/int/bool/string），成本更高。

## 6. 批次切分（2026-10-05）

- **R6a · fallback 出栈**（不依赖组合语法，立即执行）：
  删除 wire 的 `common.fallback_route` 与 native 的 `meta.fallback_route` / `fallback_route()` / `route_policy` 的 fallback 分支 / `RouteRunResult::fallback_used`；
  资产移除 `common.fallback_route` 与 `fallback.route.<kind>.*`；Kotlin 删除 fallback 选择器与 UI 状态（**旧文件的 `fallback.*` 仍被识别但忽略**，不得报「未识别旧键」）；
  extractor 不再产出 fallback；manifest 与 GLKv3 金标随字段删除重生成（wire 变化是**有意**的）。
  **门禁**：host/NDK/lint + Kotlin + cargo + 真机 43499 冷启 + 43284 app-call + AVB；并断言「主 route 失败 → **直接失败**，不再自动换 route」（错误可见）。
- **R6b · route 并入 step 自由组合**（**等维护者确认组合语法**后执行）：组合 token 字符串（推荐）或数组（需新增 wire array 类型）；
  `StepSetKind`/catalog 扩为 route × wset 的组合白名单；UI 出「推荐 + 备选」；真机 3 条 route 各跑一次。
