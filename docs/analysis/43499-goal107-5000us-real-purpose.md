# 43499 · 第 107 轮：**5000 µs 的真正作用点**（`CMP_REQUEUE_PI` 之前的 settle）✓✓✓

## 一、退役逐字（✓✓✓，`race/threads.cpp:186-192` ✓）

```cpp
ghostlock::backend::cve_2026_43499::route::RouteStatus ghostlock::race::PiRace::run() noexcept {
    while (!waiter_waiting.load() || !owner_started.load())
        usleep(profile.race_state_poll_interval_us());          // 等 waiter park ✓
    pr_info("[route] waiter parked; owner started\n");         // ← 档案里那一行 ✓
    usleep(fast_repair.load() ? **5000** : profile.race_setup_settle_us());   // ★★★ 档案值 = 50000 ✓
    errno = 0;
    long rq = support::futex_op(&wait_futex, **FUTEX_CMP_REQUEUE_PI**, 1,
                                reinterpret_cast<void *>(1), &target_futex, 0);   // ★ 紧随其后 ✓
    pr_info("[route] CMP_REQUEUE_PI ret=%ld errno=%d; waiting route_done\n", rq, errno);
}
```

## 二、这意味着什么（✓✓✓，改变优先级 ✓）

```
① 它不是"日志延迟" ✗，而是**触发序列里的等待** ✓：位于"waiter parked ✓ ⇒ **触发 requeue**"之间 ✓；
② 默认 = `race_setup_settle_us` ✓（**档案 = 50000** ✓，与夹具一致 ✓）；`fast_repair` 时 = **5000** ✓；
③ ⇒ ⇒ **⇒ 所以"5000 µs"= 把 *requeue 前的等待* 从 50 ms 压到 5 ms** ✓ ⇒ 而这**正是"拷贝能否落入
   调用者栈"的时间窗** ✗✓ ⇒ ⇒ **⇒ 它与"落不落"直接相关** ✓✓（⇒ 不是无害优化 ✗，**优先级反而很高** ✓）；
④ 且该值**只在 `fast_repair` 内生效** ✓（⇒ 即只在"预建→激活"那一次 ✓）；
⑤ 本树：`CMP_REQUEUE_PI` 确有 ✓（读数 `errno=35` ✓ 与档案同 ✓），但**没有这个 settle 开关** ✗；
   本树的对应量取自 `race.setup_settle_us` ✓（夹具 50000 ✓ hmm: ⇒ 见第 90 轮 ✓）⇒ 需要"该次用 5000" ✓。
```

## 三、因此这一"小件"其实是**关键件**（✓，修正第 106 轮的判断 ✓）

```
第 106 轮我写它属"结构性小件" ✓ —— 现更正：**它的语义很重** ✓（⇒ 直接落在 requeue 的时间窗上 ✓），
⇒ ⇒ **⇒ 仍建议 route 侧落地** ✓：给配置加一个"本次 settle 覆盖"字段 ✓ ⇒ 该次路由用它（5000 ✓）✓；
⇒ 判据：真机加一行打印（该次 settle 用了多少 ✓）⇒ ⇒ **再看 `victim uid`** ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
