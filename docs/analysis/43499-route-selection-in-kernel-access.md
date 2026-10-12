# 43499 KernelAccess：各 route 如何被区分

## 一、结论（四段 ✓）

```
① **构建时：完全不区分** ✓
   `Access() = default` 或 `Access(AccessSeam*, const TargetState*)` ✓
   逐字：「**Construction decides nothing and takes nothing**」⇒ 构造时没有 route 概念 ✓

② **初始化时：从参数里读** ✓✓
   `const auto *params = as_params(parameters);`        // kernel_access.hpp:222 具名校验
   if (params == nullptr || !params->usable_for_initialize()) ⇒ **BadParameters** ✓
   而 `usable_for_initialize()` 逐字：`return **route != profile::RouteId::Unset**;` ✓✓
   ⇒ ⇒ **⇒ 唯一判据 = "route 已被指定"**（`Unset` ⇒ 具名拒绝 ✓）
   随后：`route_ = params->route;` → `seam_->bring_up(route_, *target_)` ✓
   ⇒ ⇒ **⇒ 真正的"区分"发生在 seam 内部**（route 只是被**转交** ✓）

③ **写时：不再提 route** ✓
   `usable_for_access()` 逐字：`usable_for_initialize() && **offset_set**` ✓
   ⇒ 写只查"route 已定 ＋ offset 已给"（`offset_set` 因"0 是合法偏移" ✓ 不能当哨兵 ✓）

④ **route 的作用域 = `initialize` → `shutdown`** ✓
   逐字：「The route, its carrier and the landing gear **live from initialize to shutdown** and serve
   every access in between」✓；「**Switching routes means shutting down and initializing again**」✓
   ⇒ ⇒ **⇒ 切 route ＝ `shutdown()` 再 `initialize()`** ✓
```

## 二、为什么不按 route 分对象（逐字）

```
「**One object per backend, not per route**: the route is chosen in the parameters …
  a route object per route would turn the route into an **assembly axis**, which it is not.」
⇒ 即：route 是**参数**而非**类型**；对象数 = backend 数（1），不是 route 数。
```

## 三、`AccessParams` 携带的完整 route 面（access_params.hpp:29 ✓）

| 组 | 字段 | 用途 |
| --- | --- | --- |
| route 选择 | `route`（`RouteId`）/ `offset` / `offset_set` / `document`（`profile::Document*`） | initialize 读 route；写读 offset |
| 页几何 | `stamp_waiter_offset` / `stamp_task_offset` / `stamp_lock_offset` / `stamp_buffer_size` | 「the encoder cannot place a field in a buffer it was not told the size of」 |
| 尝试预算 | `max_attempts` / `arm_hold` / `arm_sequence` | profile 的 route 段 |
| 调用者耐心 | `park_ms` / `settle_us` / `consumer_delay_us` / `burst_calls` / `max_calls` / `fallback_lock_us` | 「a run's patience is not a fact about the kernel」 |
| 消费者调度 | `nice_modulus` / `policy` | 轮换与策略 |
| 页的两个地址 | `fake_task` / `fake_lock` / `target` / `value` | 「an attempt for a different pair is refused, because the tuple that decides what gets written lives in the **page** and not in the call」 |

```
`assembly_complete()`：上表（除 route/offset/document）**必须全齐** ✓
  逐字：「They are **one set**: a caller that supplies some of them is **not half-ready, it is wrong**,
    because the route engine reads them together.」⇒ 半个集合 ⇒ 具名拒绝，不做默认值 ✓
```

## 四、一处待办（逐字 TODO ✓）

```cpp
//TODO(access-assembly): the **document** requirement was tried here and **reverted the same day**
(2026-10-10): host `make -C core check` failed 14 cases in `cve-2026-43499 access`, because the
contract's tests declare **a route and nothing else**. The document is checked where the assembly
reads it, with a named refusal; complete when the assembly exists and its tests carry a document,
at which point this door may demand one again.
⇒ 现状：`usable_for_initialize()` **只要求 route**（document 检查被临时移走 ✗，原因在案 ✓）
```

## 五、与 ref 的对照（route 区分方式 ✓）

```
ref（ghostlock-app-ref/src/core/route/）：
  · `route_policy.hpp:114`  ⇒  `return **do_kernel5_fake_lock_route**(request);` ✓
    ⇒ **⇒ ref 用"策略函数按参数分派"** ✓（不是模板、不是多态 ✓）
  · `route_lifecycle.hpp` 是 **concept**（编译期 ✓）—— 逐字：「no vtable or indirect dispatch
    may enter the path」（因生命周期在 **PI race 窗口内** ✓）
  · 三个 route 并列：`multicast_waiter_route.cpp` / `select_stack_route.cpp` / `tcp_zerocopy_route.cpp` ✓
⇒ ⇒ **⇒ 本树的 `RouteId` ＋ `seam_->bring_up(route_, target)` 与 ref 的"策略分派"同构** ✓
   ⇒ ⇒ ⇒ **⇒ 接入时：seam 内部按 `RouteId` 分派到具体 route 实现 ✓，且**窗口内用非虚**（照抄
      `run_route_lifecycle` 的 concept 写法 ✓）**
```
