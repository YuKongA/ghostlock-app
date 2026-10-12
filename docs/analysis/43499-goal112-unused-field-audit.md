# 43499 · 第 112 轮："读进来却没人用"普查（印证第 111 轮的发现）✓✓

## 一、判据（✓，必须两处都查 ✓）

```
**错误判据** ✗："在 `main.cpp` 里出现 1 次" ⇒ 并不等于无人用（⇒ 用点可能在 `steps/` ✓）；
**正确判据** ✓：字段须在 **`main` 读入** ⇒ **`steps/route_config.cpp` 里出现为 `values.X`** ⇒ 才说明**接了**；
⇒ ⇒ 只有"**两处都没有**"（⇒ 或 steps 列表里缺席 ✓）才是"读了没用" ✗。
```

## 二、结果（✓✓，印证第 111 轮 ✓）

```
`steps/route_config.cpp` 的 `values.X` 流转列表（本轮实测 ✓）**不含 `settle_us`** ✗✓；
⇒ ⇒ **⇒ 印证第 111 轮**：`route_values.settle_us` 只被 `main` 读入 ✓（出现 3 次 = 读入 + 两处注释 ✓ 我本轮所加 ✓），
   而**在 steps 侧从未流转** ✗ ⇒ ⇒ **⇒ 它就是那个"读了没用"的字段** ✓（⇒ 现已由 `config.pre_requeue_settle_us = values.settle_us;` 接上 ✓）；
其余"在 main 出现 1 次"的字段（`consumer_delay_us` ✓ / `lock_offset` ✓ / `policy` ✓ / `post_adjust_settle_us` ✓ /
  `post_requeue_settle_us` ✓ / `task_offset` ✓ / `waiter_offset` ✓）⇒ **在 steps 侧都有 `values.X` 流转** ✓ ⇒ **属正常** ✓。
⇒ ⇒ ⇒ **⇒ 结论：同类缺口（"读了没用"）经普查只有 `settle_us` 一个** ✓（⇒ 已接 ✓）。
```

## 三、意义（✓）

```
本会话"查旧代码"找到的三个缺口（`ghost disarm` ✓ / `fast repair` ✓ / requeue 前 settle ✓）中，
**最后一个正是被这条普查独立印证**的 ✓ ⇒ ⇒ **⇒ 该模式（"读入未接"/"标志代替动作"）是本项目的真实病灶类型** ✓；
⇒ 已全部处理 ✓ ⇒ ⇒ 若 `uid=0` 仍未出现 ✗ ⇒ 病灶在**别类**（⇒ 环境/域 ✓，或"内核侧语义" ✓）。
```

## 四、门禁（✓，全绿 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
