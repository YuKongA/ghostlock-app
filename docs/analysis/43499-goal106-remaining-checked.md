# 43499 · 第 106 轮：剩余两小件的核查（② 空项；① 需新参数）✓

## 一、② 出口 `discard`：**空项** ✓

```
`PageSlot::activate` **成功后即清空槽** ✓（`memory/page_slot.hpp` 的一次性语义 ✓：activate 内 `discard()` ✓）；
且 stash⇒activate 之间只有**一次 `memcpy`** ✓（第 30 轮的注释 ✓）⇒ 无早退窗口 ✓；
⇒ ⇒ **⇒ 无需任何改动** ✓（⇒ 守则 §5 的"每条出口"在此**已自动满足** ✓）。
```

## 二、① "激活那次 5000 µs"：**不是一行** ✗

```
本树延迟来源：`main.cpp:491 num("…execution.consumer.delay_us", route_values.consumer_delay_us)` ✓（单一值 ✓）；
而**路由配置在主路由时已固化** ✗ ⇒ 事后修改 `route_values`（局部结构 ✓）**对已构造的 route 无效** ✗；
⇒ ⇒ **⇒ 必须动 route 的 config（或 race 层）** ✗ ⇒ 即第 90 轮所指的"race 层入口"✓；
⇒ ⇒ ⇒ **⇒ 其形态 = 新参数（进入延迟）+ 新管道（route/race）** ✗ ⇒ 属**结构性小件** ✓（⇒ 非一行 ✓）。
```

## 三、因此"剩余"的准确表述（✓）

```
**只剩一件** ✓：为"激活那一次"引入一个**可切换的进入延迟**（5000 µs ✓），落点二选一：
  · route 侧：给 `MulticastRouteConfig` 加一个"本次延迟覆盖"字段 ✓（⇒ 与 `main_cpu` 同层 ✓）；
  · race 侧：按退役原样 ✓（`race.reset(delay, main_cpu, consumer_cpu)` ✓）⇒ 即把延迟交给 race ✓；
⇒ 建议：**route 侧** ✓（⇒ 改动面更小、与既有 `consumer_delay_us` 同构 ✓，且本树 race 层现无该概念 ✗）。
判据：真机该次路由的延迟读数 = 5000 ✓（可加一行打印 ✓）⇒ 终判据仍是 `main: victim uid=0` ✓。
```

## 四、第 80 轮"缺失三类"的终态（✓）

```
① `WNb: private scratch repair` ⇒ **闭合** ✓（归档亦 attempt 1/3 ✓，只差打印 ✓ 已补 ✓）；
② `fast repair prebuilt` + `firing prebuilt …` ⇒ **两行齐** ✓（第 103/105 轮 ✓，行为链可核 ✓）；
③ `child is root!` / `SELinux permissive` ⇒ 成功**结果行** ✓（尚未到达 ✓）。
⇒ ⇒ **⇒ 机制侧：本会话能找到的缺失，*均已补齐或被证明等价* ✓**（⇒ 只剩 5000 µs 这一项结构性小件 ✓）。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
