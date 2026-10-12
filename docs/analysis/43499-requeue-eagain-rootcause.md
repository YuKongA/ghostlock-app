# 43499 · `requeue` 的 `EAGAIN` 根因假设：门控过晚（2026-10-12）

## 一、现象（真机逐字）

```
main: waiter lock ret=0 errno=0            ✓ 等待者拿到了锁
main: requeue ret=-1 errno=11             ✗ EAGAIN（每轮都有，15/15）
main: mcast ghost disarm ret=-1 errno=110 ✗ ETIMEDOUT（相伴出现）
```

## 二、内核语义（决定一切）

```
FUTEX_CMP_REQUEUE_PI 的 EAGAIN 意味着：uaddr 上**没有可搬的 PI 等待者**（或 val3 不符）。
⇒ 三项旋钮（val / val3 / nr_requeue）本树已按设备实测调过（multicast_route_linux.hpp:83-93 逐字）
  ⇒ 所以最可能的是"**没有等待者**"。
```

## 三、本树的次序（关键）

```
multicast_route.hpp
  waiter 侧：逐字注释「round 122 ✓：waiter_waiting **移到 park 返回之后**（原先在 park 之前）」
     ⇒ 即：shared_.waiter_waiting.store(1) 发生在 wait_requeue_pi_until(...) **返回之后**。
  main 侧  requeue()：while (waiter_waiting == 0 || owner_started == 0) yield();  ← 等的正是它
     ⇒ ⇒ **⇒ 于是 requeue 必然发生在 park **已经结束**之后** ⇒ 那一次 CMP_REQUEUE_PI 面对的是
        "已经没有等待者"的 uaddr ⇒ ⇒ **EAGAIN** ✓✓
而旧码（641aaf6）的次序相反：requeue 发生在等待者**仍 park 期间** ✓（其成功形态是 EDEADLK ✓）。
```

## 四、为什么当年改晚了（不能直接回退）

```
round 122 的原文理由：内核可能在 waiter **尚未 park** 时搬链 ⇒ **拷贝无处落** ⇒ 写不发生 ✗
⇒ ⇒ 所以"提前置标志"会回到那个问题；"推到 park 之后"又造成 EAGAIN ✗ ⇒ **两头都有代价** ✓
```

## 五、两全的做法（旧码的解法）

```
旧码：waitress 侧先做完整准备（stamp 已下毒、字已就位）⇒ 主线程**等四标志齐 + usleep(200000)**
      ⇒ 再 CMP_REQUEUE_PI ⇒ 它在"等待者已在 park 中"的窗口内发出 ✓
本树：只需把门槛从"park 已返回"改为"**已进入 park 并已过宽限**"：
  ① waiter：在**调用 park 之前**置 ready（并保证 stamp/字已就位）；
  ② main：等 ready 之后再睡一段宽限（沿用已有的 settle 机制，值仍走 wire ✗ 不写死）⇒ 再 requeue。
⇒ 判据：真机 `main: requeue ret=-1 errno=35`（EDEADLK ✓ 旧码成功形态）⇒ 再看 `main: victim uid=0`。
```
