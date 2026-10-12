# 43499 · 第 121 轮（终·2）：**病因精确到"置位在 park 之前"** ✓✓✓

## 一、逐字（✓✓✓）

```cpp
// waiter_main（:473-479 ✓）
shared_.waiter_waiting.store(1);                                   // ★ **先置位** ✗（此时还没 park ✓）
const std::int64_t park_us = static_cast<std::int64_t>(config_.park_ms) * 1000;
const std::int32_t parked = Ops::wait_requeue_pi_until(&shared_.chain_word, &shared_.target_word, park_us);   // ★ **才 park** ✓
// requeue()（:341-347 ✓）
while (shared_.waiter_waiting.load() == 0 || shared_.owner_started.load() == 0) { Ops::yield(); }   // ⇒ 置位即放行 ✗
```

## 二、推论（✓✓✓）

```
⇒ ⇒ **⇒ `requeue` 可能在 waiter **真正 park 之前**就发起** ✗ ✓；
⇒ 于是 `CMP_REQUEUE_PI` 搬链时，那份 waiter/页**尚未进到可被替换的状态** ✗ ⇒ ⇒ **⇒ 拷贝无处落 ⇒ 写不发生** ✓✓；
⇒ ⇒ **⇒ 这与 14/14 `comm len=15`（写没落）**完全一致** ✓✓✓。
⇒ 另一处佐证：`execute()` 在 `requeue()` **之后**才 `while (route_done == 0) yield();` ✓（`:257-260` ✓）
   ⇒ ⇒ 即"先发 requeue、再等触发完成" ✗ ⇒ **⇒ 两件事的顺序被设计成并发** ✗（退役为**串行** ✓）。
```

## 三、修复点（✓，两处小改，同批 ✓）

```
① waiter 侧：park **返回后**再置一个"已 park"标志 ✓（或把 `waiter_waiting` 改为在 park 返回后置 ✓）；
② requeue 侧：把等待条件由 `waiter_waiting` 改为**该"已 park"标志** ✓（⇒ 于是 requeue 必在 park 之后 ✓）；
⇒ 与退役同形（退役：park 完成 ⇒ race 线程 requeue ✓）；
⇒ 判据（两步）：① `main: victim comm after len=` 变 **0/8** ✓；② `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
