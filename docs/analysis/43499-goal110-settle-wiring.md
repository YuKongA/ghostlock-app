# 43499 · 第 110 轮：`pre_requeue_settle_us` 数据侧 1-2 步（结构与赋值 ✓）

## 一、改动（✓，两处 ✓）

```cpp
// ① steps/route_config.hpp（RouteConfigValues ✓，紧邻 main_cpu ✓）
std::int64_t pre_requeue_settle_us = 0;     // ★ 0 = 不启用 ✓
// ② steps/route_config.cpp（赋值点 ✓，紧邻 main_cpu 那行 ✓）
config.pre_requeue_settle_us = static_cast<std::int32_t>(values.pre_requeue_settle_us);
config.main_cpu = static_cast<std::int32_t>(values.main_cpu);
```

## 二、状态（✓，全绿且不改现状 ✓）

```
默认 **0** ⇒ 行为不变 ✓；门禁：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓；
⇒ ⇒ **⇒ 现在整条链已通到最后一段** ✓：`RouteConfigValues` ✓ ⇒ `RouteConfig` ✓ ⇒ 使用点（`cmp_requeue_pi` 之前 ✓）✓。
```

## 三、下一步（✓，只差"从文档读一个值" ✓）

```
在 `main.cpp` 填 `RouteConfigValues` 的那处（⇒ 与 `:491` 的 `num("…consumer.delay_us", …)` 同型 ✓）加一行：
  `num("backend.cve_2026_43499.execution.race.setup_settle_us", route_values.pre_requeue_settle_us);` ✓
⇒ 夹具该键 = **50000** ✓（第 90 轮核过 ✓）⇒ ⇒ 于是**每次路由都会等 50 ms** ✓（= 退役的默认相位 ✓）；
⇒ 再把"激活那一次"（`repair_prebuilt` ✓）覆写为 **5000** ✓（退役 `fast_repair ? 5000 : …` ✓ ⇒ 由标记派生 ✓ 不写死 ✗）；
⇒ 判据：真机加一行打印（该次 settle = 50000 / 激活时 5000 ✓）⇒ ⇒ **再看 `victim uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
