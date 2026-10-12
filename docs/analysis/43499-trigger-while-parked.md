# 43499 · "走链"的钥匙（round 89）：trigger 必须在等待者**仍挂着**时发生

## 旧代码的原文（退役 `route/multicast_waiter_route.cpp`）

```c
stamp_result = setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, sizeof(stamp));
stamp_errno = errno;
/* The copy lands before the family check, so -EADDRNOTAVAIL still means
 * the waiter shape is on the stack now. */
if (attempt < arm_sequence || (stamp_result != 0 && stamp_errno != EADDRNOTAVAIL)) continue;
consumer_go.store(attempt);            // 让 consumer 动（走链）
for (spin…) __asm__("yield");          // arm_hold
consumer_go.store(0);
while (consumer_inflight) yield;
if (consumer_success.load() > 0) break;
```

⇒ 两点被这句话钉住 ✓：

1. **`EADDRNOTAVAIL`（= 我日志里的 `stamp=-1/99` ✓）意味着拷贝已经落地** ✓（不是失败 ✓）；
2. 拷贝落在"**此刻还在那里**的那份 waiter 形状"上 ✓ ⇒ 因此触发必须发生在**等待者仍处于 park 中**的时刻 ✓。

## 我的引擎与此相反（结构错位）

```
waiter_main():  lock_pi(chain) → yield 等 owner → wait_requeue_pi_until(…)   ← 1 s 后**超时返回**
                TEMPWAIT ret=-1 errno=110（真机 ✓）
                trigger()   ← ✗ 此时栈上/slab 里已无 waiter ⇒ 拷贝无处可落
requeue():      调用线程等 waiter_waiting+owner_started → settle → cmp_requeue_pi（−1/EAGAIN ✓）
```

⇒ 结果就是二分实验量到的 **`at2000=0x0`**（内核一个字节都没改）✓ —— 与"无 waiter 可覆盖"完全一致 ✓。

## 修法（结构改动，下一步执行）

把 `trigger()` 从"等待者 park 之后"移到**调用线程的循环里、等待者仍在 park 期间** ✓（与退役同序 ✓）：

1. `execute()`：起线程后先让 waiter 进入 park（已有 `waiter_waiting` ✓），**再由调用线程**跑 `trigger()` ✓；
2. `trigger()` 返回后（拷贝已落 ✓）再 `consumer_go`（走链 ✓）、等处 → 结束这一轮 ✓；
3. `waiter` 侧：park 返回后不再触发 ✗，只做"过期截止期再锁一次"的收尾 ✓（退役同款 ✓）；
4. 判据：`TEMPBISECT at2000` 应变为**非零**（机制在写 ✓）⇒ 再换回受害者地址 ⇒ `victim uid=0` ✓。
