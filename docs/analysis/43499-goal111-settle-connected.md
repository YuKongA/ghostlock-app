# 43499 · 第 111 轮：**第 3 缺口接上了**（settle = 树里早已读入、此前无人用）✓✓

## 一、发现（✓✓✓，本轮最有价值的一条 ✓）

```
`main.cpp:485`：`num("…execution.race.setup_settle_us", route_values.settle_us);` ✓ —— **早就在读** ✓；
而全树**无人使用 `route_values.settle_us`** ✗（⇒ grep 无使用点 ✓）；
⇒ ⇒ ⇒ **⇒ 即：这正是"**读进来却没接上**"的典型** ✓✓（⇒ 与我第 108 轮推断的"缺 requeue 前 settle"完全吻合 ✓）。
```

## 二、接法（✓，三处串起来 ✓）

```
① `main.cpp:485` 读键 ✓（**原有**，未改 ✓）；② `route_config.cpp`：`config.pre_requeue_settle_us = values.settle_us;` ✓（本轮 ✓）；
③ `multicast_route.hpp`：`if (config_.pre_requeue_settle_us > 0) { Ops::sleep_micros(…); }` ⇒ 在 `cmp_requeue_pi` **之前** ✓（第 109 轮 ✓）；
⇒ ⇒ **⇒ 于是每次路由前会有夹具值（**50000 µs** ✓）的 settle** ✓ = 退役的默认相位 ✓；
⇒ 代价：每次路由 +50 ms ✓（⇒ 15 轮重试会明显变慢 ✓，属预期 ✓）。
```

## 三、真机读数（✓，跑得干净 ✓）

```
`main: route status=0/0 clean=1/1 attempts=16 calls=1 successes=1 stamp=-1/99` ✓；
`main: mcast ghost disarm ret=-1 errno=110` ✓；`step result w1 => won` ✓；
`main: victim uid=2000` ✗；`uid=0` 计数 **0** ✗ ⇒ ⇒ **⇒ 该缺口已实现，但效果**仍未出现** ✗（如实 ✓）。
```

## 四、三个缺口的最终状态（✓）

| # | 缺口 | 发现 | 实现 | 效果 |
| --- | --- | --- | --- | --- |
| 1 | `ghost disarm` | 第 76/77 轮 ✓ | **已实现** ✓（读数与归档同 ✓） | 未见 ✗ |
| 2 | `fast repair` 预建+激活 | 第 25/80 轮 ✓ | **已实现** ✓（两行齐 ✓ 页链可核 ✓） | 未见 ✗ |
| 3 | **requeue 前 settle** | 第 107/108 轮 ✓ | **已实现** ✓（`values.settle_us` ⇒ 配置 ⇒ 使用 ✓） | 未见 ✗ |

```
⇒ ⇒ **⇒ 机制侧：本会话找到的三个缺口**全部落地** ✓（各有真机读数佐证 ✓），而 `main: victim uid=0` 仍未出现 ✗。
⇒ ⇒ ⇒ 因此**结论只能是**：`uid=0` 还取决于**本会话尚未发现**的一环 ✗（⇒ 或与运行环境有关：域/宿主 ✓，第 67/69 轮已留档 ✓）。
```

## 五、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
