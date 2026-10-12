# 计划：把 `RouteExec` 从虚基类改为编译期策略（裁定 ④ · L 级 · 2026-10-12）

## 0. 裁定（用户逐字）

> 「`arm`／`execute` 我怎么知道在不在 PI 内，不确定就改成编译期log」

⇒ 读作：**不去证 PI 边界** ✗ ⇒ **直接改编译期** ✓ ⇒ 这样**两头都安全** ✓✓（⇒ 关 critic **P1** ✓）。

## 1. 事实（已查实 ✓）

```
· `RouteExec`（`seam/multicast_window.hpp:29-41`）：抽象基类 —— `virtual ~RouteExec()` ＋
  `virtual bool execute(…)` ✗ ／ **`virtual bool arm(…) = 0`** ✗
· **实现者只有一个** ✓✓：`class MulticastRoute final : public RouteExec`（`seam/multicast_route.hpp:211`）
  ⇒ ⇒ **⇒ 多态没有第二个实现 ⇒ 纯属多余** ✓（⇒ 与 `pi_window.hpp:16`「窗口体内不做间接调用」冲突 ✓）
· 持有者（⇒ 改动面）：`MulticastWindow(RouteExec &exec, …)` ✓（`multicast_window.hpp:54/84`）／
  `MulticastWaiterAccess(seam::RouteExec &, …)` ✓（`access_multicast_waiter.hpp:45`）／
  `window_attempt.hpp:23 seam::RouteExec *route` ✓
· **先例** ✓：`race/route_lifecycle.hpp` 与 `pi_race` 的 policy **本来就是编译期** ✓ ⇒
  其注释的理由正是"跑在 PI 竞态窗口内" ⇒ ⇒ **⇒ 与本次裁定同源** ✓✓
```

## 2. 改法（三步 ✓）

```
① `RouteExec` ⇒ **概念**（concept）：`requires` 出 `execute(address, value, offset)` 与 `arm(attempt)` ✓
   形状仿 `route_lifecycle.hpp` 的既有 concept ✓（⇒ 本树风格 ✓）
② `MulticastWindow`／`MulticastWaiterAccess` ⇒ **模板**（⇒ 以具体类型参数化 ✓）
③ `window_attempt.hpp` 的 `RouteExec *route` ⇒ 同法模板化 ✓；⇒ 上层 `window_runner`／`access_binder`
   ／`access_multicast_waiter` 跟着模板化 ✓（⇒ **模板会传染** ✗ ⇒ 这是本改动的**真实代价** ✓）
```

## 3. 验收（可失败 ✓）

```
· `grep -c virtual core/src/backend/cve_2026_43499/seam/multicast_window.hpp` ⇒ **0** ✓✓（⇒ 可失败 ✓）
· 把 `multicast_window.hpp` **纳入**扫描（`Makefile` 的 WINDOW-DIRTY sed 列表 ＋ 守卫 ✓）
  ⇒ ⇒ 而这正是 critic **P1** 要求的"纳入 ＋ 消解虚调用" ✓✓
· 三线门禁：check **109 PASS** 起只增不减 ✓／android **零告警** ✓／markdownlint 0 ✓
· 真机：判据**不变**（⇒ 日志读数应与改动前**逐字相同** ✓ —— 因为只改分派方式 ✗，不改行为 ✓）
```

## 4. 沿革（用户强制确认 ✓）

```
· **越过哪条**：`pi_window.hpp:16`「the window BODY … makes no indirect call」—— 改动后**该禁令**真的成立 ✓
  （⇒ 此前它被 `virtual arm/execute` 违反 ✓）；⇒ 同时本计划**不改**禁令文本 ✓（⇒ 因为改完就**不需要**改 ✓）
· **谁确认**：用户（逐字见 §0 ✓）；⇒ 代价：**模板传染** ✗（⇒ 记为本批的技术债 ✓，⇒ 不跨批 ✓）
· **不做**：不去证 PI 边界 ✗（⇒ 用户裁定明确免去 ✓）；⇒ 也不改 `pi_race` ✓（⇒ 它本就是编译期 ✓）
```
