# 43499 · 第 121 轮：**顺序差异定位到"送页 vs requeue"** ✓✓✓

## 一、本树的触发（✓✓，逐字 ✓）

```cpp
// multicast_route.hpp:404-422
void trigger() noexcept {
    const int fd = Ops::open_datagram();                       // ★ datagram socket ✓
    for (attempt = 1..max_attempts) {
        if (!build_stamp()) break;                              // 每轮**重新下毒** ✓（退役同义 ✓）
        result = Ops::set_block_source(fd, stamp_, config_.stamp.buffer_size);   // ★ **把页送进内核** ✓
    …
}
```

```
⇒ “送页”原语**正确** ✓（⇒ 正是退役注释里的 "skb page recycling" 那条路 ✓）；
而它由 **waiter 线程在 park 之后立刻**调用 ✓（`:484 trigger();` ✓）。
```

## 二、与退役的顺序对照（✓✓✓，逐字 ✓）

```
退役（`race/threads.cpp:58-80` ✓）：waiter park ✓ ⇒ **race 线程 `CMP_REQUEUE_PI`**（内核搬链、去读那页 ✓）
                                ⇒ 然后 `route_done.store(1)` ✓ ⇒ `UNLOCK_PI(chain_futex)` ✓。
本树：waiter park ✓ ⇒ **waiter 线程立刻 `trigger()`**（送页 ✓）⇒ `route_done` ✓ ⇒ `unlock_pi(chain_word)` ✓；
      而 `requeue()`（含 `cmp_requeue_pi` ✓）**由调用线程（owner ✓）**执行 ✓ ⇒ ⇒ **⇒ 两者分属不同线程 ✗**。
⇒ ⇒ **⇒ 即："送页"与"requeue"的**先后**在退役里是确定的（requeue 先 ✓），
   在本树里**由两线程竞态决定** ✗✓ ⇒ ⇒ **⇒ 这与"写没落"的读数完全吻合** ✓（14/14 comm=15 ✗）。
```

## 三、下一步（✓，两条可选，都可核 ✓）

```
① **把 requeue 移回 waiter 线程、并置于 `trigger()` 之前** ✓（⇒ 与退役同形 ✓，顺序确定 ✓）；
   —— 或 ——
② 在 owner 侧**先 requeue 再等** waiter 送页 ✓（⇒ 需要一处同步 ✓）；
⇒ 判据：`main: victim comm after len=` **变 0/8** ✓（⇒ 本树自带的即时旁证 ✓）⇒ 然后看 `victim uid` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
