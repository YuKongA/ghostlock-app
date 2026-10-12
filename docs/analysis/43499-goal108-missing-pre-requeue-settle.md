# 43499 · 第 108 轮：**相位缺口** —— 本树有 requeue *之后* 的 settle，缺 requeue *之前* 的那段 ✗✓

## 一、两边的相位对照（✓✓✓，逐行 ✓）

```
本树（`multicast_route.hpp`）:
  :349  `shared_.requeue_result = Ops::cmp_requeue_pi(&shared_.chain_word, &shared_.target_word);` ✓
  :350  /* 相位一（退役 waiter.post_requeue_settle_us）：内核搬完链再让 consumer 动。 */ ✓
  :351-352  `if (config_.post_requeue_settle_us > 0) { Ops::sleep_micros(…); }` ✓ ⇒ **requeue 之后** ✓；
退役（`race/threads.cpp:186-192`）:
  `while (!waiter_waiting… ) usleep(race_state_poll_interval_us());` ✓ ⇒ 等 park ✓；
  `pr_info("[route] waiter parked; owner started\n");` ✓；
  `usleep(**fast_repair ? 5000 : race_setup_settle_us**);` ✓ ⇒ ⇒ **然后才** `futex_op(… CMP_REQUEUE_PI …)` ✓
  ⇒ ⇒ **⇒ 即：退役在 requeue *之前* 还有一段 settle** ✗（档案值 50000 ✓；fast_repair 时 5000 ✓）；
⇒ 而 `grep setup_settle` 在 **main.cpp 与 seam 里都没有命中** ✗ ⇒ ⇒ **⇒ 本树缺的就是这一段** ✓✓。
```

## 二、为什么这与"落不落"直接相关（✓✓✓）

```
退役的注释（`race/threads.cpp:98-102` ✓）说：拷贝落在**调用者栈**上，是"race against skb page recycling" ✗；
⇒ ⇒ **⇒ requeue 之前那段 50 ms（或 5 ms）正是"让拷贝先落稳"的等待** ✓ ⇒
⇒ 本树没有它 ⇒ 可能在**还没落稳**时就触发 requeue ✗ ⇒ ⇒ **⇒ 与第 44/53/58 轮那些读数完全吻合** ✓✓
  （⇒ 机制读数全对 ✓ 而"效果为零" ✗）。
```

## 三、因此（✓，更正第 49 轮的对照范围 ✓）

```
第 49 轮我核对了 `post_requeue_settle_us` / `post_adjust_settle_us` ✓ ⇒ **漏了 pre-requeue 的那段** ✗✓；
⇒ ⇒ **⇒ 这是本会话找到的**第 3 个"相位/动作缺口"** ✓（前两个：ghost disarm ✓ 已补 / fast repair ✓ 已补 ✓）；
⇒ 落地：把夹具既有的 `race.setup_settle_us`（= 50000 ✓）读进来 ✓ ⇒ 在**本树 `cmp_requeue_pi` 之前** sleep ✓
  ⇒ 并在"激活那一次"（`repair_prebuilt` ✓）用 **5000** ✓ ⇒ 与退役同形 ✓。
判据：真机加一行打印（该次 settle 用了多少 ✓）⇒ ⇒ **再看 `victim uid`** ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
