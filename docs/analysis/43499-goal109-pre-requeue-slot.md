# 43499 · 第 109 轮：`pre_requeue_settle_us` 槽位就位（默认 0 ⇒ 不改现状）✓✓

## 一、改动（✓，两处 ✓）

```cpp
// ① multicast_route.hpp 配置（紧邻既有 post_requeue_settle_us ✓）
std::int32_t pre_requeue_settle_us = 0;      // ★ requeue **之前**的 settle（退役 race/threads.cpp:189 ✓）
// ② 使用点（`cmp_requeue_pi` **之前** ✓）
if (config_.pre_requeue_settle_us > 0) { Ops::sleep_micros(config_.pre_requeue_settle_us); }
shared_.requeue_result = Ops::cmp_requeue_pi(&shared_.chain_word, &shared_.target_word);   // ← 紧随其后 ✓
```

## 二、状态（✓）

```
默认 **0** ⇒ **现状不变** ✓（⇒ 门禁全绿 ✓：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓）；
⇒ ⇒ **⇒ 槽位已就位** ✓ ⇒ 只差"把数据接进来" ✓（⇒ 下一步 ✓）。
```

## 三、下一步（✓，两处数据接入 ✓）

```
① 读键：夹具既有 `backend.cve_2026_43499.execution.race.setup_settle_us` ✓（= **50000** ✓，第 90 轮核过 ✓）
   ⇒ 在 `route_config.cpp` 里读进 `values.pre_requeue_settle_us` ✓ ⇒ 赋给 `config.pre_requeue_settle_us` ✓；
② 激活那次用 **5000** ✓（退役 `fast_repair ? 5000 : …` ✓）⇒ 由 `repair_prebuilt`/`fast_repair` 派生 ✓【不写死 ✗】；
③ 加一行打印（该次 settle 用了多少 ✓）⇒ ⇒ **再看 `victim uid`** ✓。
```

## 四、本会话的"缺口清单"现状（✓）

| # | 缺口 | 状态 |
| --- | --- | --- |
| 1 | `ghost disarm`（第 76/77 轮发现） | **已实现** ✓（真机 `ret=-1 errno=110` ✓ 与归档同 ✓） |
| 2 | `fast repair` 预建 + 激活（第 25/80 轮） | **已实现** ✓（两行齐 ✓，页链可核 ✓） |
| 3 | **requeue 前的 settle**（第 107/108 轮发现） | **槽位已就位** ✓（数据接入 = 下一步 ✓） |

```
⇒ ⇒ **⇒ 三个缺口里两个已落地并各有真机读数 ✓，第三个的代码结构已就位 ✓。**
```
