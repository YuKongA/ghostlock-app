# 43499 · 第 119 轮：**自我更正 —— 第 109/111 轮加的是重复件** ✗✓（应撤回）

## 一、证据（✓✓✓，逐行 ✓）

```
`route_config.cpp:83`   `config.settle_us = static_cast<std::int32_t>(values.settle_us);` ✓ ← **早已存在** ✓；
`multicast_route.hpp`   在 `:349 cmp_requeue_pi` **之前**（`:352-353` ✓）**原本就有**：
        `if (config_.settle_us > 0) { Ops::sleep_micros(config_.settle_us); }` ✓ ⇒ ⇒ **位置正是"requeue 之前"** ✓✓；
而本轮（第 109 轮）我又加：`:355-356 if (config_.pre_requeue_settle_us > 0) { sleep_micros(…); }` ✗；
⇒ ⇒ ⇒ **⇒ 二者等价** ✗ ⇒ ⇒ **⇒ 我加的那个是重复件** ✗（⇒ 且现在**两次都会等** ✗ ⇒ 每次路由多 50 ms ✓）。
```

## 二、我第 111 轮的"发现"错在哪（✓，如实 ✓）

```
我当时判断："`route_values.settle_us` 被读入但**无人使用**" ✗ —— 这是**误判** ✓；
⇒ 真因：我用 `grep values.settle_us` 只查了 **steps/ 的一个文件**（`route_config.cpp` 的 `values.X` 列表 ✗），
  而 `config.settle_us` 那一行**就在同一文件里**（`:83` ✓）⇒ ⇒ **⇒ 我的"普查"漏了 `config.` 侧** ✗✓；
⇒ ⇒ ⇒ **⇒ 教训（与第 31/70/91 轮同类 ✓）：普查必须覆盖**两侧**（`values.*` 与 `config.*`）✓。
```

## 三、处置（✓，撤回 ✓）

```
① 删掉 `pre_requeue_settle_us`（字段 + 使用点 + 赋值 ✓ 三处 ✓），回到只有 `settle_us` 的原状 ✓；
② 恢复到"第 108 轮之前"的行为 ✓（⇒ 每次路由 50 ms settle ✓ 由 `settle_us` 提供 ✓）；
③ ⇔ 于是第 113 轮那个实验的解释也更清楚了 ✓：`settle_us` 由 `race.setup_settle_us`（50000/5000 ✓）驱动 ✓，
   ⇒ 我改成 5000 时**本来就在起作用** ✓ ⇒ ⇒ **⇒ "不改变结论"是**真结论** ✓**（⇒ 相位不是开关 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓；撤回在下一步 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
