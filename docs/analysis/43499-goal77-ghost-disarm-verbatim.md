# 43499 · 第 77 轮：**ghost disarm 的逐字实现**（本会话最重的发现）✓✓✓

## 一、动作（✓✓✓，`race/threads.cpp:65-75` 逐字 ✓）

```cpp
race->route_status = controller.execute(request);        // 路线执行 ✓
if (route_needs_ghost_disarm(profile)) {                 // 判据：policy.multicast ⇒ mcast 为 true ✓
    /* remove_waiter() left this thread's pi_blocked_on pointing at the
     * reclaimed stack waiter. **Force one final slow-path removal while the
     * stack frame is still alive**, matching the 5.x multicast primitive's
     * disarm step. **Without this, thread exit leaves a walkable dangling
     * ghost and the next mm_struct spray can panic the kernel.** */
    uint32_t dummy_pi = 0x80000000U | static_cast<uint32_t>(getpid());
    struct timespec expired = {.tv_sec = 0, .tv_nsec = 0};
    errno = 0;
    long disarm = support::futex_op(&dummy_pi, **FUTEX_LOCK_PI**, 0, &expired, nullptr, 0);
    pr_info("mcast ghost disarm ret=%ld errno=%d\n", disarm, errno);   // ← 归档那行 ✓
}
```

## 二、它做什么（✓✓✓）

```
① 造**伪用户地址** `dummy_pi = 0x80000000 | getpid()` ✓（⇒ 保证不会被当成真实 futex ✓）；
② 对它发 **`FUTEX_LOCK_PI`**，超时 **0**（⇒ 立即过期 ✓）⇒ 触发**慢路径** ✓；
③ 目的（注释逐字 ✓）：**"Force one final slow-path removal while the stack frame is still alive"** ✓；
④ 缘由：`remove_waiter()` 已把该线程的 `pi_blocked_on` 指向**被回收的栈 waiter** ✗ ⇒
   ⇒ **若不摘除 ⇒ 线程退出会留下"可被 walk 的幽灵"** ✗ ⇒ ⇒ **⇒ walk 指向的就是这个幽灵** ✓✓
   （⇒ 而不是我页里那三个字所描述的 target ✗）⇒ ⇒ **⇒ 这与"页/编码全对而写不生效"完全吻合** ✓✓。
```

## 三、本树现状与修法（✓，已可落码 ✓）

```
本树：**无该动作、无该读数** ✗（第 76 轮已证 ✓）；
修法（一处 ✓）：在本树 waiter 线程**路线执行之后、线程退出之前** ✓（⇒ 栈帧尚存活 ✓）补同形动作：
  伪地址 `0x80000000|pid` ✓ + `FUTEX_LOCK_PI` 且超时 0 ✓ + 打印 `ret/errno` ✓；
判据：真机出现与归档同形**`mcast ghost disarm ret=-1 errno=110`** ✓ ⇒ ⇒ **再看 `main: victim uid=0`** ✓✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
